#include "../src/mapimport.h"
#include <stdio.h>
int main(void) {
    MapImport mi;
    int n = mapimport_scan(&mi);
    for (int i = 0; i < n; i++) mapimport_run(&mi, i);
    printf("%d %d %d\n", n, mi.imported, mi.failed);
    return 0;
}
