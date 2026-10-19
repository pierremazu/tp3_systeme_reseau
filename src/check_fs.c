#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "tosfs.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <image_filesystem>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *img_path = argv[1];

    int fd = open(img_path, O_RDWR);
    if (fd < 0) {
        perror("Erreur lors de l'ouverture du fichier image");
        return EXIT_FAILURE;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("Erreur fstat");
        close(fd);
        return EXIT_FAILURE;
    }
    size_t fs_size = st.st_size;

    void *fs_mem = mmap(NULL, fs_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fs_mem == MAP_FAILED) {
        perror("Erreur mmap");
        close(fd);
        return EXIT_FAILURE;
    }

    struct tosfs_superblock *sb = (struct tosfs_superblock *) fs_mem;

    printf("VERIFICATION SUPERBLOCK \n");
    printf("Magic attendu  : 0x%x\n", TOSFS_MAGIC);
    printf("Magic lu       : 0x%x\n", sb->magic);

    if (sb->magic != TOSFS_MAGIC) {
        fprintf(stderr, "Erreur : Nombre magique incorrect, ce n'est pas un système tosfs valide.\n");
        munmap(fs_mem, fs_size);
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Système de fichiers reconnu\n\n");

    printf("Taille d'un bloc     : %u octets\n", sb->block_size);
    printf("Nombre de blocs      : %u\n", sb->blocks);
    printf("Nombre max d'inodes  : %u\n", sb->inodes);
    printf("Numéro inode Root    : %u\n\n", sb->root_inode);

    printf("Block Bitmap (32 bits) : " PRINTF_BINARY_PATTERN_INT32 "\n",
           PRINTF_BYTE_TO_BINARY_INT32(sb->block_bitmap));
    printf("Inode Bitmap (32 bits) : " PRINTF_BINARY_PATTERN_INT32 "\n\n",
           PRINTF_BYTE_TO_BINARY_INT32(sb->inode_bitmap));


    struct tosfs_inode *inodes = (struct tosfs_inode *) ((char *)fs_mem + TOSFS_INODE_BLOCK * TOSFS_BLOCK_SIZE);

    //Inode Root
    struct tosfs_inode root = inodes[sb->root_inode];
    printf("=== INODE ROOT (Inode %u) ===\n", sb->root_inode);
    printf("Inode n°      : %u\n", root.inode);
    printf("Bloc alloué   : %u\n", root.block_no);
    printf("Mode (octal)  : 0%o\n", root.mode);
    printf("Perms (octal) : 0%o\n", root.perm);
    printf("UID / GID     : %u / %u\n", root.uid, root.gid);
    printf("Taille        : %u octets\n", root.size);
    printf("Nombre liens  : %u\n\n", root.nlink);

    //Exploration du contenu du répertoire
    printf("=== CONTENU DU RÉPERTOIRE ROOT (Bloc %u) ===\n", root.block_no);
    struct tosfs_dentry *dentries = (struct tosfs_dentry *) ((char *)fs_mem + root.block_no * TOSFS_BLOCK_SIZE);

    int max_dentries = TOSFS_BLOCK_SIZE / sizeof(struct tosfs_dentry);
    int count = 0;

    for (int i = 0; i < max_dentries; i++) {
        //entrée valide si Inode != 0
        if (dentries[i].inode != 0) {
            printf("  [%02d] Inode: %u \t Nom: \"%s\"\n", i, dentries[i].inode, dentries[i].name);
            count++;
        }
    }

    if (count == 0) {
        printf("  (Le répertoire racine est actuellement vide)\n");
    }

    //Nettoyage
    munmap(fs_mem, fs_size);
    close(fd);
    return EXIT_SUCCESS;
}