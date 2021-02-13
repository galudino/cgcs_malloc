/*!
    \file       cgcs_malloc_test.c
    \brief      Client source file for cgcs_malloc: malloc implementation

    \author     Gemuele Aludino
    \date       12 Feb 2021
 */

// use directive to omit cgcs_ prefix from cgcs_malloc(size) and cgcs_free(ptr)
#define USE_CGCS_MALLOC
#include "cgcs_malloc.h"

/*!
    \brief      Program execution begins and ends here.

    \param[in]  argc    Command line argument count
    \param[in]  argv    Command line arguments

    \return     0 on success, non-zero on failure
 */
int main(int argc, const char *argv[]) {
    void *ptr = malloc(BUFSIZ);
    free(ptr);

    return EXIT_SUCCESS;
}
