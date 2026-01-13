#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <portaudio.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

typedef struct {
    int sockfd;
    PaStream *in_stream;
    int frames_per_buffer;
} TxCtx;

typedef struct {
    int sockfd;
    PaStream *out_stream;
    int frames_per_buffer;
} RxCtx;

typedef struct {
    const char *server;
    int sample_rate;
    int frames_per_buffer;
    int list_only;
    int input_device;
    int output_device;
} AppConfig;

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static void print_usage(const char *name) {
    fprintf(stderr,
            "usage: %s [-s host:port] [-r sample_rate] [-f frames] [-l] [-i input_device] [-o output_device]\n",
            name);
}

static int parse_host_port(const char *input, char *host, size_t host_len, char *port, size_t port_len) {
    const char *colon = strrchr(input, ':');
    if (!colon || colon == input || *(colon + 1) == '\0') {
        return -1;
    }
    size_t hlen = (size_t)(colon - input);
    if (hlen >= host_len) {
        return -1;
    }
    memcpy(host, input, hlen);
    host[hlen] = '\0';
    if (strlen(colon + 1) >= port_len) {
        return -1;
    }
    strcpy(port, colon + 1);
    return 0;
}

static void *tx_thread(void *arg) {
    TxCtx *ctx = (TxCtx *)arg;
    int16_t *samples = calloc((size_t)ctx->frames_per_buffer, sizeof(int16_t));
    uint8_t *send_buf = calloc((size_t)ctx->frames_per_buffer * 2, sizeof(uint8_t));
    if (!samples || !send_buf) {
        fprintf(stderr, "tx alloc failed\n");
        running = 0;
        free(samples);
        free(send_buf);
        return NULL;
    }

    while (running) {
        PaError err = Pa_ReadStream(ctx->in_stream, samples, ctx->frames_per_buffer);
        if (err != paNoError) {
            if (err == paInputOverflowed) {
                continue;
            }
            fprintf(stderr, "input read error: %s\n", Pa_GetErrorText(err));
            running = 0;
            break;
        }
        for (int i = 0; i < ctx->frames_per_buffer; i++) {
            uint16_t v = (uint16_t)samples[i];
            send_buf[i * 2] = (uint8_t)(v & 0xff);
            send_buf[i * 2 + 1] = (uint8_t)((v >> 8) & 0xff);
        }
        ssize_t sent = send(ctx->sockfd, send_buf, (size_t)ctx->frames_per_buffer * 2, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("udp send");
            running = 0;
            break;
        }
    }

    free(samples);
    free(send_buf);
    return NULL;
}

static void *rx_thread(void *arg) {
    RxCtx *ctx = (RxCtx *)arg;
    int16_t *samples = calloc((size_t)ctx->frames_per_buffer, sizeof(int16_t));
    uint8_t *recv_buf = calloc((size_t)ctx->frames_per_buffer * 2, sizeof(uint8_t));
    if (!samples || !recv_buf) {
        fprintf(stderr, "rx alloc failed\n");
        running = 0;
        free(samples);
        free(recv_buf);
        return NULL;
    }

    while (running) {
        ssize_t n = recv(ctx->sockfd, recv_buf, (size_t)ctx->frames_per_buffer * 2, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                memset(samples, 0, (size_t)ctx->frames_per_buffer * sizeof(int16_t));
                PaError err = Pa_WriteStream(ctx->out_stream, samples, ctx->frames_per_buffer);
                if (err != paNoError && err != paOutputUnderflowed) {
                    fprintf(stderr, "output write error: %s\n", Pa_GetErrorText(err));
                    running = 0;
                }
                continue;
            }
            perror("udp recv");
            running = 0;
            break;
        }
        if (n != ctx->frames_per_buffer * 2) {
            memset(samples, 0, (size_t)ctx->frames_per_buffer * sizeof(int16_t));
            PaError err = Pa_WriteStream(ctx->out_stream, samples, ctx->frames_per_buffer);
            if (err != paNoError && err != paOutputUnderflowed) {
                fprintf(stderr, "output write error: %s\n", Pa_GetErrorText(err));
                running = 0;
            }
            continue;
        }
        for (int i = 0; i < ctx->frames_per_buffer; i++) {
            uint16_t v = (uint16_t)recv_buf[i * 2] | ((uint16_t)recv_buf[i * 2 + 1] << 8);
            samples[i] = (int16_t)v;
        }
        PaError err = Pa_WriteStream(ctx->out_stream, samples, ctx->frames_per_buffer);
        if (err != paNoError) {
            if (err == paOutputUnderflowed) {
                continue;
            }
            fprintf(stderr, "output write error: %s\n", Pa_GetErrorText(err));
            running = 0;
            break;
        }
    }

    free(samples);
    free(recv_buf);
    return NULL;
}

static void list_devices(void) {
    int count = Pa_GetDeviceCount();
    if (count < 0) {
        fprintf(stderr, "PortAudio device error: %s\n", Pa_GetErrorText(count));
        return;
    }
    printf("PortAudio devices:\n");
    for (int i = 0; i < count; i++) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if (!info) {
            continue;
        }
        const PaHostApiInfo *host = Pa_GetHostApiInfo(info->hostApi);
        printf("[%d] %s (%s) in:%d out:%d\n", i, info->name, host ? host->name : "unknown",
               info->maxInputChannels, info->maxOutputChannels);
    }
}

int main(int argc, char **argv) {
    AppConfig cfg = {
        .server = "127.0.0.1:9000",
        .sample_rate = 16000,
        .frames_per_buffer = 320,
        .list_only = 0,
        .input_device = -1,
        .output_device = -1,
    };

    int opt;
    while ((opt = getopt(argc, argv, "s:r:f:li:o:h")) != -1) {
        switch (opt) {
        case 's':
            cfg.server = optarg;
            break;
        case 'r':
            cfg.sample_rate = atoi(optarg);
            break;
        case 'f':
            cfg.frames_per_buffer = atoi(optarg);
            break;
        case 'l':
            cfg.list_only = 1;
            break;
        case 'i':
            cfg.input_device = atoi(optarg);
            break;
        case 'o':
            cfg.output_device = atoi(optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
            break;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.sample_rate <= 0 || cfg.frames_per_buffer <= 0) {
        fprintf(stderr, "sample rate and frames must be positive\n");
        return 1;
    }

    char host[256];
    char port[32];
    if (parse_host_port(cfg.server, host, sizeof(host), port, sizeof(port)) != 0) {
        fprintf(stderr, "invalid server address: %s\n", cfg.server);
        return 1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai));
        return 1;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
        perror("connect");
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }
    freeaddrinfo(res);

    struct timeval rcv_timeout;
    rcv_timeout.tv_sec = 0;
    rcv_timeout.tv_usec = 50000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout)) != 0) {
        perror("setsockopt SO_RCVTIMEO");
        close(sockfd);
        return 1;
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "portaudio init failed: %s\n", Pa_GetErrorText(err));
        close(sockfd);
        return 1;
    }

    if (cfg.list_only) {
        list_devices();
        Pa_Terminate();
        close(sockfd);
        return 0;
    }

    PaStream *in_stream = NULL;
    PaStream *out_stream = NULL;
    PaStreamParameters in_params;
    PaStreamParameters out_params;

    PaDeviceIndex in_dev = (cfg.input_device >= 0) ? cfg.input_device : Pa_GetDefaultInputDevice();
    PaDeviceIndex out_dev = (cfg.output_device >= 0) ? cfg.output_device : Pa_GetDefaultOutputDevice();

    if (in_dev == paNoDevice || out_dev == paNoDevice) {
        fprintf(stderr, "no suitable input/output device found\n");
        Pa_Terminate();
        close(sockfd);
        return 1;
    }

    const PaDeviceInfo *in_info = Pa_GetDeviceInfo(in_dev);
    const PaDeviceInfo *out_info = Pa_GetDeviceInfo(out_dev);
    if (!in_info || !out_info) {
        fprintf(stderr, "failed to query device info\n");
        Pa_Terminate();
        close(sockfd);
        return 1;
    }

    memset(&in_params, 0, sizeof(in_params));
    in_params.device = in_dev;
    in_params.channelCount = 1;
    in_params.sampleFormat = paInt16;
    in_params.suggestedLatency = in_info->defaultLowInputLatency;

    memset(&out_params, 0, sizeof(out_params));
    out_params.device = out_dev;
    out_params.channelCount = 1;
    out_params.sampleFormat = paInt16;
    out_params.suggestedLatency = out_info->defaultLowOutputLatency;

    err = Pa_OpenStream(&in_stream, &in_params, NULL, cfg.sample_rate, cfg.frames_per_buffer, paClipOff, NULL, NULL);
    if (err != paNoError) {
        fprintf(stderr, "open input stream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        close(sockfd);
        return 1;
    }

    err = Pa_OpenStream(&out_stream, NULL, &out_params, cfg.sample_rate, cfg.frames_per_buffer, paClipOff, NULL, NULL);
    if (err != paNoError) {
        fprintf(stderr, "open output stream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(in_stream);
        Pa_Terminate();
        close(sockfd);
        return 1;
    }

    if (Pa_StartStream(in_stream) != paNoError || Pa_StartStream(out_stream) != paNoError) {
        fprintf(stderr, "start streams failed\n");
        Pa_CloseStream(in_stream);
        Pa_CloseStream(out_stream);
        Pa_Terminate();
        close(sockfd);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    TxCtx tx = {sockfd, in_stream, cfg.frames_per_buffer};
    RxCtx rx = {sockfd, out_stream, cfg.frames_per_buffer};

    pthread_t tx_tid;
    pthread_t rx_tid;
    if (pthread_create(&tx_tid, NULL, tx_thread, &tx) != 0) {
        fprintf(stderr, "failed to start tx thread\n");
        running = 0;
    }
    if (pthread_create(&rx_tid, NULL, rx_thread, &rx) != 0) {
        fprintf(stderr, "failed to start rx thread\n");
        running = 0;
    }

    while (running) {
        usleep(10000);
    }

    pthread_join(tx_tid, NULL);
    pthread_join(rx_tid, NULL);

    Pa_StopStream(in_stream);
    Pa_StopStream(out_stream);
    Pa_CloseStream(in_stream);
    Pa_CloseStream(out_stream);
    Pa_Terminate();

    close(sockfd);
    return 0;
}
