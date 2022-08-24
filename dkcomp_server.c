/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - web version backend */

#if defined(__WIN32__)
#include <winsock2.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <dkcomp.h>

enum PRG_STATE {
    PRG_RUNNING,
    PRG_QUIT
};

struct CINFO {
    struct MHD_PostProcessor *processor;
    unsigned char *input;
    size_t    input_size;
    size_t decomp_offset;
    int comp_type; /* format, 0-n */
    int comp_mode; /* 0 = compress, 1 = decompress */
};

static int load_file (const char *fn, unsigned char **buf, size_t *size) {

    FILE *f = fopen(fn, "r");
    if (f == NULL) {
        fprintf(stderr, "Failed to open \"%s\".\n", fn);
        return 1;
    }

    if (fseek(f, 0, SEEK_END) == -1
    || (*size = ftell(f))     == -1u
    ||  fseek(f, 0, SEEK_SET) == -1) {
        fprintf(stderr, "Failed to seek \"%s\".\n", fn);
        fclose(f);
        return 1;
    }

    if (*size < 128 || *size > 262144) {
        fprintf(stderr, "\"%s\" is an unusual size. (%zd)\n", fn, *size);
        fclose(f);
        return 1;
    }

    if ((*buf = malloc(1+*size)) == NULL) {
        fprintf(stderr, "Failed to allocate memory for \"%s\" buffer.\n", fn);
        fclose(f);
        return 1;
    }

    if (fread(*buf, 1, *size, f) != *size) {
        fprintf(stderr, "Error attempting to to read \"%s\".\n", fn);
        fclose(f);
        free(*buf);
        return 1;
    }
    (*buf)[*size] = 0;
    fclose(f);
    return 0;
}

static enum MHD_Result iterate_post (
    void *arg,
    enum MHD_ValueKind kind,
    const char *key,
    const char *filename,
    const char *content_type,
    const char *transfer_encoding,
    const char *data,
    uint64_t ofs,
    size_t size
) {
    (void)kind;
    (void)filename;
    (void)content_type;
    (void)transfer_encoding;
    (void)ofs;

    struct CINFO *cinfo = arg;

    if (!strcmp(key, "comp_mode")) {
        cinfo->comp_mode = *data == '1';
        return MHD_YES;
    }
    else if (!strcmp(key, "comp_type")) {
        cinfo->comp_type = strtol(data, NULL, 10);
        return MHD_YES;
    }
    else if (!strcmp(key, "decomp_offset")) {
        cinfo->decomp_offset = strtol(data, NULL, 10);
        return MHD_YES;
    }
    else if (!strcmp(key, "file")) {
        unsigned char *buf;
        if ((cinfo->input_size + size) > (1 << 26)) {
            fprintf(stderr, "Error: Exceeded maximum allowed POST size.\n");
            free(cinfo->input);
            cinfo->input = NULL;
            return MHD_NO;
        }
        buf = realloc(cinfo->input, cinfo->input_size + size);
        if (buf == NULL) {
            fprintf(stderr, "Error: Failed to allocate for data buffer.\n");
            free(cinfo->input);
            cinfo->input = NULL;
            return MHD_NO;
        }
        cinfo->input = buf;
        memcpy(&cinfo->input[cinfo->input_size], data, size);
        cinfo->input_size += size;
        return MHD_YES;
    }
    return MHD_NO;
}

static enum MHD_Result respond_message (
    struct MHD_Connection *connection,
    const char *msg_in,
    int status_code
) {
    struct MHD_Response *response;
    int e;
    size_t msg_size = strlen(msg_in);
    char *msg = malloc(msg_size + 1);
    if (msg == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for a message.\n");
        return MHD_NO;
    }
    strcpy(msg, msg_in);
    response = MHD_create_response_from_buffer(msg_size, msg, MHD_RESPMEM_MUST_FREE);
    if (response == NULL)
        return MHD_NO;
    e = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return e;
}

static enum MHD_Result respond_binary (
    struct MHD_Connection *connection,
    unsigned char *data,
    size_t size
) {
    struct MHD_Response *response;
    int e;
    char length[32];
    snprintf(length, 32, "%zd", size);
    response = MHD_create_response_from_buffer(size, data, MHD_RESPMEM_MUST_FREE);
    if (response == NULL)
        return MHD_NO;
    e = MHD_add_response_header(response, "Content-Length", length);
    e = MHD_add_response_header(response, "Content-Disposition", "attachment");
    e = MHD_add_response_header(response, "Content-Type", "application/octet-stream");
    e = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return e;
}


static enum MHD_Result respond_file (
    struct MHD_Connection *connection,
    char *filename
) {
    struct MHD_Response *response;
    unsigned char *buf;
    size_t size;
    int e;
    if (load_file(filename, &buf, &size))
        return respond_message(connection, "Failed to load our HTML file.", MHD_HTTP_INTERNAL_SERVER_ERROR);
    response = MHD_create_response_from_buffer(size, buf, MHD_RESPMEM_MUST_FREE);
    if (response == NULL)
        return MHD_NO;
    e = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return e;
}

static enum MHD_Result http_response (
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
) {
    (void)version;

    int e = 0;

    if (!strcmp(url, "/exec") && !strcmp(method, "POST")) {
        /* on the first iteration we set up a processor and a */
        /* struct to hold all of our parameters */
        struct CINFO *cinfo = *con_cls;
        if (cinfo == NULL) {
            cinfo = calloc(1, sizeof(struct CINFO));
            if (cinfo == NULL)
                return MHD_NO;
            cinfo->processor = MHD_create_post_processor(
                connection,
                1 << 16,
                iterate_post,
                cinfo
            );
            if (cinfo->processor == NULL) {
                free(cinfo);
                return MHD_NO;
            }
            *con_cls = cinfo;
            return MHD_YES;
        }

        /* on subsequent iterations we call the processor */
        else if (*upload_data_size != 0) {
            MHD_post_process(
                cinfo->processor,
                upload_data,
               *upload_data_size
            );
            *upload_data_size = 0;
            return MHD_YES;
        }

        /* and if we make it this far we're done, so we can */
        /* process the data and determine the response */

        unsigned char *data = NULL;
        size_t size = 0;

        if (cinfo->comp_mode
        &&  cinfo->decomp_offset >= cinfo->input_size) {
            e = respond_message(connection, "Decompression offset is larger than input size.", MHD_HTTP_INTERNAL_SERVER_ERROR);
        }
        else {
            e = (cinfo->comp_mode
              ? dk_decompress_mem_to_mem
              :   dk_compress_mem_to_mem)(
                cinfo->comp_type,
                &data,
                &size,
                cinfo->input      + cinfo->decomp_offset,
                cinfo->input_size - cinfo->decomp_offset
            );

            /* send the response, either an error or binary data */
            if (e)
                e = respond_message(connection, dk_get_error(e), MHD_HTTP_INTERNAL_SERVER_ERROR);
            else
                e = respond_binary(connection, data, size);
        }

        /* all done */
        MHD_destroy_post_processor(cinfo->processor);
        free(cinfo->input);
        free(cinfo);
    }
    else if (!strcmp(url, "/ping")) {
        e = respond_message(connection, "", MHD_HTTP_OK);
    }
    else if (!strcmp(url, "/quit")) {
        enum PRG_STATE *status = cls;
        *status = PRG_QUIT;
        e = respond_message(connection, "", MHD_HTTP_OK);
        puts("Received quit command.");
    }
    else if (
        !strcmp(url, "/")
    ||  !strcmp(url, "/default.html")
    ||  !strcmp(url, "/index.html")
    ||  !strcmp(url, "/dkcomp_server.html")
    ) {
        e = respond_file(connection, "dkcomp_server.html");
    }
    else {
        e = respond_message(connection,
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />"
            "<title>404 - Not Found</title>\n"
            "</head>\n"
            "<body>\n"
            "404 - Not Found\n"
            "<br>\n"
            "(<a href=\"index.html\">try here</a>)\n"
            "</body>\n"
            "</html>",
            404
        );
    }
    return e;
}

static void open_url (unsigned port) {
    char url[64];
#if defined(__WIN32__)
    snprintf(url, 64, "http://127.0.0.1:%u", port);
    ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    snprintf(url, 64, "xdg-open http://127.0.0.1:%u", port);
    system(url);
#endif

}

struct PRG_ARGS {
    unsigned short  port;
    unsigned short sleep;
    int launch;
};

static int server_loop (struct PRG_ARGS *args) {
    enum PRG_STATE status = PRG_RUNNING;

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY
      | MHD_USE_ERROR_LOG,
        args->port,
        NULL, NULL,
        &http_response, &status,
        MHD_OPTION_END
    );

    if (daemon == NULL) {
        puts("Failed to start MHD Daemon.");
        return 1;
    }

    printf("Server active on 127.0.0.1:%u\n", args->port);

    if (args->launch)
        open_url(args->port);

    for (;;) {
#if defined(__WIN32)
    Sleep(args->sleep);
#else
    usleep(args->sleep);
#endif
        if (status == PRG_QUIT)
            break;
    }
    MHD_stop_daemon(daemon);
    return 0;
}

static int parse_args (int argc, char *argv[], struct PRG_ARGS *args) {
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts(
                "dkcomp web interface\n\n"
                "options:\n"
                "  --nolaunch   ; don't automatically open the page\n"
                "  --port  NUM  ; run server on specified port        (default: 1234)\n"
                "  --sleep NUM  ; check for requests at this interval (default:  100)\n"
                "  --help       ; display this help text"
            );
            return 1;
        }
        else if (!strcmp(argv[i], "--port") && ++i < argc) {
            args->port = strtol(argv[i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--sleep") && ++i < argc) {
            args->sleep = strtol(argv[i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--nolaunch")) {
            args->launch = 0;
        }
        else {
            fprintf(stderr, "unknown argument: \"%s\"\n", argv[i]);
            return 1;
        }
    }
    return 0;
}

int main (int argc, char *argv[]) {

    struct PRG_ARGS args;
    args.port   = 1234;
    args.sleep  = 100;
    args.launch = 1;

    if (parse_args(argc, argv, &args) 
    ||  server_loop(&args))
        return 1;

    return 0;
}

