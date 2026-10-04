/* chaintest.exe <cert.der> - print CertGetCertificateChain trust status (SKJ Wine debug tool) */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    FILE *f; long n; BYTE *buf; PCCERT_CONTEXT c; PCCERT_CHAIN_CONTEXT chain;
    CERT_CHAIN_PARA para = { sizeof(para) };
    DWORD i, j;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: chaintest cert.der\n"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); buf = malloc(n); fread(buf, 1, n, f); fclose(f);
    if (!(c = CertCreateCertificateContext(X509_ASN_ENCODING, buf, n))) { printf("bad cert %lx\n", GetLastError()); return 1; }
    if (!CertGetCertificateChain(NULL, c, NULL, c->hCertStore, &para, 0, NULL, &chain)) { printf("chain failed %lx\n", GetLastError()); return 1; }
    printf("chain error=0x%08lx info=0x%08lx\n", chain->TrustStatus.dwErrorStatus, chain->TrustStatus.dwInfoStatus);
    for (i = 0; i < chain->cChain; i++)
        for (j = 0; j < chain->rgpChain[i]->cElement; j++)
            printf(" elem %lu: error=0x%08lx info=0x%08lx\n", j,
                   chain->rgpChain[i]->rgpElement[j]->TrustStatus.dwErrorStatus,
                   chain->rgpChain[i]->rgpElement[j]->TrustStatus.dwInfoStatus);
    return 0;
}
