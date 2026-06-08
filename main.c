#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* Function Declarations */
int read_content_id(const char *src_dir, char *content_id_out);
void clean_path(const char *input, char *output);
void process_directory(FILE *fp, const char *base_src, const char *current_rel, int indent);
void print_indent(FILE *fp, int level);
int write_content_id_txt(const char *gp4p_path, const char *content_id);

int main(int argc, char *argv[]) {
    char src_dir[MAX_PATH];
    char out_path[MAX_PATH];
    char main_app_location[MAX_PATH];
    char content_id[37]; /* 36 characters + 1 null terminator */
    FILE *fp = NULL;

    /* Require exactly 3 operational arguments (plus executable path) */
    if (argc < 4) {
        printf("Usage: gengp4p-p.exe <source_folder> <output_gp4p_path> <main_app_location>\n");
        printf("Example: gengp4p-p.exe .\\PCSH00241_patch .\\patch.gp4p \"VitaPKG\\UP9000-PCSA00042_00-FB0EU0FULL000100.pkg\"\n");
        return 1;
    }

    clean_path(argv[1], src_dir);
    strcpy(out_path, argv[2]);
    strcpy(main_app_location, argv[3]);

    /* 1. Extract, validate, and patch content_id using +10/+13 rules from param.sfo */
    if (!read_content_id(src_dir, content_id)) {
        return 1; 
    }

    /* 2. Open output file */
    fp = fopen(out_path, "w");
    if (!fp) {
        printf("Error: Could not create output file: %s\n", out_path);
        return 1;
    }

    /* 3. Write modified patch project header containing main_app_location variables */
    fprintf(fp, "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n");
    fprintf(fp, "<psproject fmt=\"gp4p\" gene_ver=\"2.31\">\n");
    fprintf(fp, "  <rootdir name=\"\"\n");
    fprintf(fp, "     passcode=\"00000000000000000000000000000000\"\n");
    fprintf(fp, "     pub_ver=\"01.00\"\n");
    fprintf(fp, "     proj_type=\"psp2_patch\"\n");
    fprintf(fp, "     drm_type=\"Local\"\n");
    fprintf(fp, "     content_id=\"%s\"\n", content_id);
    fprintf(fp, "     app_pkg_path=\"%s\"\n", main_app_location);
    fprintf(fp, "     patch_type=\"cumulative_for_hybrid\">\n");

    /* 4. Process patch folders recursively */
    process_directory(fp, src_dir, "", 4);

    fprintf(fp, "  </rootdir>\n");
    fprintf(fp, "</psproject>\n");

    fclose(fp);
    printf("Successfully generated %s\n", out_path);

    /* 5. Save the content ID to companion contentID.txt */
    if (write_content_id_txt(out_path, content_id)) {
        printf("Successfully generated companion contentID.txt\n");
    }

    return 0;
}

void clean_path(const char *input, char *output) {
    int len;
    strcpy(output, input);
    len = strlen(output);
    while (len > 0 && (output[len - 1] == '\\' || output[len - 1] == '/')) {
        output[len - 1] = '\0';
        len--;
    }
}

void print_indent(FILE *fp, int level) {
    int i;
    for (i = 0; i < level; i++) {
        fprintf(fp, " ");
    }
}

int write_content_id_txt(const char *gp4p_path, const char *content_id) {
    char txt_path[MAX_PATH];
    char *last_slash = NULL;
    FILE *txt_file = NULL;

    strcpy(txt_path, gp4p_path);
    last_slash = strrchr(txt_path, '\\');
    if (!last_slash) {
        last_slash = strrchr(txt_path, '/');
    }

    if (last_slash) {
        strcpy(last_slash + 1, "contentID.txt");
    } else {
        strcpy(txt_path, "contentID.txt");
    }

    txt_file = fopen(txt_path, "w");
    if (!txt_file) {
        printf("Warning: Could not create companion file: %s\n", txt_path);
        return 0;
    }

    fprintf(txt_file, "%s", content_id);
    fclose(txt_file);
    return 1;
}

/* Scans param.sfo using the strict +10/+13 structural rules, handles gbc type patching, and reads the ID */
int read_content_id(const char *src_dir, char *content_id_out) {
    char sfo_path[MAX_PATH];
    FILE *sfo = NULL;
    unsigned char *buffer = NULL;
    long file_size = 0;
    long i = 0;
    int found = 0;

    sprintf(sfo_path, "%s\\sce_sys\\param.sfo", src_dir);
    sfo = fopen(sfo_path, "rb"); 
    if (!sfo) {
        printf("Error: Missing file or path unreadable: %s\n", sfo_path);
        return 0;
    }

    fseek(sfo, 0, SEEK_END);
    file_size = ftell(sfo);
    fseek(sfo, 0, SEEK_SET);

    if (file_size < 36) {
        printf("Error: param.sfo file size is too small.\n");
        fclose(sfo);
        return 0;
    }

    buffer = (unsigned char *)malloc(file_size);
    if (!buffer) {
        printf("Error: Out of memory buffer handling.\n");
        fclose(sfo);
        return 0;
    }

    fread(buffer, 1, file_size, sfo);
    fclose(sfo);

    /* Loop parsing file looking for layout match points */
    for (i = 6; i <= (file_size - 30); i++) {
        
        /* 1. Evaluate current byte as reference point candidate '-' (0x2D) */
        if (buffer[i] == 0x2D) {
            
            /* 2. Enforce explicit structural layout rules: +10 is '_' (0x5F) and +13 is '-' (0x2D) */
            if (buffer[i + 10] == 0x5F && buffer[i + 13] == 0x2D) {
                
                long start_offset = i - 6; /* Beginning character sits 6 bytes back */
                
                if (start_offset >= 0 && (start_offset + 36) <= file_size) {
                    
                    /* Check if the 2 bytes before the content ID start are "63 00" hex */
                    if (start_offset >= 2 && buffer[start_offset - 2] == 0x63 && buffer[start_offset - 1] == 0x00) {
                        printf("gbc type is detected\n");
                        
                        /* Re-open file in writeable mode to patch the data headers directly */
                        sfo = fopen(sfo_path, "r+b");
                        if (sfo) {
                            fseek(sfo, start_offset - 2, SEEK_SET);
                            fputc(0x00, sfo);
                            fputc(0x00, sfo);
                            fclose(sfo);
                        } else {
                            printf("Warning: gbc type detected but could not patch file (write protection or file in use).\n");
                        }
                    }
                    
                    /* Populate output pointer with string contents */
                    memcpy(content_id_out, &buffer[start_offset], 36);
                    content_id_out[36] = '\0'; 
                    found = 1;
                    break; 
                }
            }
        }
    }

    free(buffer);

    if (!found) {
        printf("Error: Could not locate Content ID sequence inside %s\n", sfo_path);
        return 0;
    }

    return 1;
}

void process_directory(FILE *fp, const char *base_src, const char *current_rel, int indent) {
    char search_path[MAX_PATH];
    WIN32_FIND_DATA find_data;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    char new_rel[MAX_PATH];
    const char* base_folder_name = NULL;

    if (stricmp(current_rel, "sce_sys") == 0) {
        print_indent(fp, indent);
        fprintf(fp, "<dir name=\"about\">\n");
        print_indent(fp, indent + 2);
        fprintf(fp, "<file name=\"right.suprx\" reftype=\"mem_about\"/>\n");
        print_indent(fp, indent);
        fprintf(fp, "</dir>\n");

        print_indent(fp, indent);
        fprintf(fp, "<file name=\"clearsign\" reftype=\"mem_cs\"/>\n");
        print_indent(fp, indent);
        fprintf(fp, "<file name=\"keystone\" reftype=\"mem_ks\"/>\n");
    }

    if (strlen(current_rel) == 0) {
        sprintf(search_path, "%s\\*", base_src);
    } else {
        sprintf(search_path, "%s\\%s\\*", base_src, current_rel);
    }

    /* PASS 1: Find and print FILES only */
    hFind = FindFirstFile(search_path, &find_data);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }

            if (strlen(current_rel) == 0) {
                strcpy(new_rel, find_data.cFileName);
            } else {
                sprintf(new_rel, "%s\\%s", current_rel, find_data.cFileName);
            }

            base_folder_name = strrchr(base_src, '\\');
            if (base_folder_name == NULL) {
                base_folder_name = base_src;
            } else {
                base_folder_name++; 
            }

            print_indent(fp, indent);
            fprintf(fp, "<file name=\"%s\" reffile=\"%s\\%s\"/>\n", 
                    find_data.cFileName, base_folder_name, new_rel);

        } while (FindNextFile(hFind, &find_data) != 0);
        
        FindClose(hFind);
    }

    /* PASS 2: Find and recurse into DIRECTORIES only */
    hFind = FindFirstFile(search_path, &find_data);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                continue;
            }

            if (strlen(current_rel) == 0) {
                strcpy(new_rel, find_data.cFileName);
            } else {
                sprintf(new_rel, "%s\\%s", current_rel, find_data.cFileName);
            }

        
