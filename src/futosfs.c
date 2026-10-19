#define FUSE_USE_VERSION 26

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "tosfs.h"

static void *fs_mem = NULL;
static size_t fs_size = 0;
static struct tosfs_superblock *sb = NULL;
static struct tosfs_inode *inodes = NULL;


// Fonctions utilitaires d'accès à la mémoire
static inline void *get_block(uint32_t block_no) {
    return (char *)fs_mem + (block_no * TOSFS_BLOCK_SIZE);
}

// fonction getattr
static void futosfs_ll_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void) fi; //pour éviter un warning à l'execution

 //On vérifie que l'inode est entre 1 et 31, et est allouée dans la bitmap
    if (ino < 1 || ino >= 32 || !(sb->inode_bitmap & (1 << ino))) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    struct tosfs_inode *ti = &inodes[ino];
    struct stat stbuf;
    memset(&stbuf, 0, sizeof(stbuf));

    // on remplis la structure stat
    stbuf.st_ino = ino;
    stbuf.st_mode = ti->mode | ti->perm;
    stbuf.st_nlink = ti->nlink;
    stbuf.st_uid = ti->uid;
    stbuf.st_gid = ti->gid;
    stbuf.st_size = ti->size;
    stbuf.st_blocks = (ti->size > 0) ? 1 : 0;
    stbuf.st_blksize = TOSFS_BLOCK_SIZE;

    // Réponse à FUSE (ici 1.0 = durée de cache de 1 seconde)
    fuse_reply_attr(req, &stbuf, 1.0);
}

// readdir
static void futosfs_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                              off_t off, struct fuse_file_info *fi)
{
    (void) fi;
    fuse_reply_err(req, ENOSYS);
}

// lookup
static void futosfs_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    fuse_reply_err(req, ENOENT);
}

// open et read
static void futosfs_ll_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    fuse_reply_open(req, fi);
}

static void futosfs_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
                            off_t off, struct fuse_file_info *fi)
{
    (void) fi;
    fuse_reply_err(req, ENOSYS);
}

//déclaration des operations fuse low level

static struct fuse_lowlevel_ops futosfs_oper = {
    .lookup   = futosfs_ll_lookup,
    .getattr  = futosfs_ll_getattr,
    .readdir  = futosfs_ll_readdir,
    .open     = futosfs_ll_open,
    .read     = futosfs_ll_read,
};


// Fonction pour charger l'image tosfs avec mmap

static int load_fs(const char *img_path)
{
    int fd = open(img_path, O_RDWR);
    if (fd < 0) {
        perror("open(img_path)");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    fs_size = st.st_size;

    fs_mem = mmap(NULL, fs_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd); // on ferme le descripteur

    if (fs_mem == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    sb = (struct tosfs_superblock *) fs_mem;
    if (sb->magic != TOSFS_MAGIC) {
        fprintf(stderr, "Erreur, Magic Number invalide : 0x%x au lieu de 0x%x)\n", sb->magic, TOSFS_MAGIC);
        munmap(fs_mem, fs_size);
        return -1;
    }

    inodes = (struct tosfs_inode *) get_block(TOSFS_INODE_BLOCK);
    printf("Image chargée avec succès de taille: %zu octets\n", fs_size);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <image_tosfs> <mountpoint> [options FUSE]\n", argv[0]);
        fprintf(stderr, "Exemple: %s test_tosfs_files /tmp/futosfs -d\n", argv[0]);
        return 1;
    }

    //Le 1er argument est le fichier image
    const char *img_path = argv[1];
    if (load_fs(img_path) != 0) {
        return 1;
    }

    //On retire le 1er argument (l'image) de argv pour ne laisser à FUSE que : argv[0] (programme) et a    rgv[2..] (mountpoint, options FUSE).
    int fuse_argc = argc - 1;
    char **fuse_argv = malloc(sizeof(char *) * fuse_argc);
    fuse_argv[0] = argv[0];
    for (int i = 1; i < fuse_argc; i++) {
        fuse_argv[i] = argv[i + 1];
    }

    struct fuse_args args = FUSE_ARGS_INIT(fuse_argc, fuse_argv);
    struct fuse_chan *ch;
    char *mountpoint;
    int err = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
        (ch = fuse_mount(mountpoint, &args)) != NULL) {
        struct fuse_session *se;

        se = fuse_lowlevel_new(&args, &futosfs_oper, sizeof(futosfs_oper), NULL);
        if (se != NULL) {
            if (fuse_set_signal_handlers(se) != -1) {
                fuse_session_add_chan(se, ch);
                err = fuse_session_loop(se);
                fuse_remove_signal_handlers(se);
                fuse_session_remove_chan(ch);
            }
            fuse_session_destroy(se);
        }
        fuse_unmount(mountpoint, ch);
    }

    fuse_opt_free_args(&args);
    free(fuse_argv);

    if (fs_mem && fs_mem != MAP_FAILED) {
        munmap(fs_mem, fs_size);
    }

    return err ? 1 : 0;
}