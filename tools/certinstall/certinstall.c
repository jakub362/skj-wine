/*
 * certinstall.exe - SKJ Wine helper
 *
 * Replaces PowerShell's New-SelfSignedCertificate / Import-PfxCertificate,
 * which Wine cannot run. Imports a PFX (cert + private key) into a system
 * certificate store and prints the SHA-1 thumbprint (uppercase hex), which
 * is what vendor installers store in the registry.
 *
 * Usage: certinstall.exe <file.pfx> [password] [--machine|--user] [--store My]
 */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BYTE *read_file(const char *path, DWORD *len)
{
    FILE *f = fopen(path, "rb");
    BYTE *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc(n);
    if (buf && fread(buf, 1, n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = (DWORD)n;
    return buf;
}

int main(int argc, char **argv)
{
    const char *pfx_path = NULL, *pass = "", *store_name = "My";
    BOOL machine = TRUE, machine_keys = TRUE;
    CRYPT_DATA_BLOB blob;
    WCHAR wpass[256];
    HCERTSTORE pfx, dst;
    PCCERT_CONTEXT ctx = NULL, added = NULL;
    BYTE hash[20];
    DWORD hlen = sizeof(hash), i;
    int found = 0, positional = 0;

    for (i = 1; i < (DWORD)argc; i++) {
        if (!strcmp(argv[i], "--machine")) machine = TRUE;
        else if (!strcmp(argv[i], "--user")) machine = FALSE;
        else if (!strcmp(argv[i], "--user-keys")) machine_keys = FALSE;
        else if (!strcmp(argv[i], "--store") && i + 1 < (DWORD)argc) store_name = argv[++i];
        else if (positional == 0) { pfx_path = argv[i]; positional++; }
        else if (positional == 1) { pass = argv[i]; positional++; }
    }
    if (!pfx_path) {
        fprintf(stderr, "usage: certinstall.exe <file.pfx> [password] [--machine|--user] [--user-keys] [--store My]\n");
        return 2;
    }

    blob.pbData = read_file(pfx_path, &blob.cbData);
    if (!blob.pbData) { fprintf(stderr, "cannot read %s\n", pfx_path); return 1; }
    MultiByteToWideChar(CP_UTF8, 0, pass, -1, wpass, 256);

    pfx = PFXImportCertStore(&blob, wpass,
                             CRYPT_EXPORTABLE | ((machine && machine_keys) ? CRYPT_MACHINE_KEYSET : CRYPT_USER_KEYSET));
    if (!pfx) { fprintf(stderr, "PFXImportCertStore failed: 0x%lx\n", GetLastError()); return 1; }

    dst = CertOpenStore(CERT_STORE_PROV_SYSTEM_A, 0, 0,
                        (machine ? CERT_SYSTEM_STORE_LOCAL_MACHINE : CERT_SYSTEM_STORE_CURRENT_USER),
                        store_name);
    if (!dst) { fprintf(stderr, "CertOpenStore(%s) failed: 0x%lx\n", store_name, GetLastError()); return 1; }

    while ((ctx = CertEnumCertificatesInStore(pfx, ctx))) {
        DWORD sz = 0;
        /* only the leaf with a private key is interesting */
        if (!CertGetCertificateContextProperty(ctx, CERT_KEY_PROV_INFO_PROP_ID, NULL, &sz)) continue;
        if (!CertAddCertificateContextToStore(dst, ctx, CERT_STORE_ADD_REPLACE_EXISTING, &added)) {
            fprintf(stderr, "CertAddCertificateContextToStore failed: 0x%lx\n", GetLastError());
            return 1;
        }
        /* Wine's PFXImportCertStore does not record CRYPT_MACHINE_KEYSET in the
         * key provider info, so later lookups search the user keyset and fail
         * with NTE_BAD_KEYSET. Fix the property up ourselves. */
        if (machine && machine_keys) {
            DWORD pisz = 0;
            CRYPT_KEY_PROV_INFO *pi;
            if (CertGetCertificateContextProperty(added, CERT_KEY_PROV_INFO_PROP_ID, NULL, &pisz) &&
                (pi = malloc(pisz)) &&
                CertGetCertificateContextProperty(added, CERT_KEY_PROV_INFO_PROP_ID, pi, &pisz)) {
                if (!(pi->dwFlags & CRYPT_MACHINE_KEYSET)) {
                    pi->dwFlags |= CRYPT_MACHINE_KEYSET;
                    if (!CertSetCertificateContextProperty(added, CERT_KEY_PROV_INFO_PROP_ID, 0, pi))
                        fprintf(stderr, "warning: could not set CRYPT_MACHINE_KEYSET: 0x%lx\n", GetLastError());
                }
                free(pi);
            }
        }
        if (CertGetCertificateContextProperty(added, CERT_SHA1_HASH_PROP_ID, hash, &hlen)) {
            for (i = 0; i < hlen; i++) printf("%02X", hash[i]);
            printf("\n");
        }
        CertFreeCertificateContext(added);
        found++;
    }
    CertCloseStore(dst, 0);
    CertCloseStore(pfx, 0);
    if (!found) { fprintf(stderr, "no certificate with a private key in %s\n", pfx_path); return 1; }
    return 0;
}
