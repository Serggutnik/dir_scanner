#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define MAX_PATH 4096
#define MAX_TOP 100

struct Task {
    char path[MAX_PATH];
    int depth;
};

struct BigFile {
    char path[MAX_PATH];
    long long size;
};

struct Task *queue = NULL;
int queue_size = 0;
int queue_cap = 0;
int busy = 0;

long long total_files = 0;
long long total_size = 0;
long long total_dirs = 0;
long long errors = 0;

struct BigFile top[MAX_TOP];
int top_count = 0;

int max_depth = 3;
int top_n = 10;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void push_task(const char *path, int depth)
{
    if (queue_size == queue_cap) {
        queue_cap = queue_cap == 0 ? 1024 : queue_cap * 2;
        queue = realloc(queue, queue_cap * sizeof(struct Task));
    }
    strcpy(queue[queue_size].path, path);
    queue[queue_size].depth = depth;
    queue_size++;
}

void add_to_top(const char *path, long long size)
{
    if (top_count < top_n) {
        strcpy(top[top_count].path, path);
        top[top_count].size = size;
        top_count++;
        return;
    }
    int min = 0;
    for (int i = 1; i < top_count; i++)
        if (top[i].size < top[min].size)
            min = i;
    if (size > top[min].size) {
        strcpy(top[min].path, path);
        top[min].size = size;
    }
}

void scan_dir(struct Task *task)
{
    DIR *dir = opendir(task->path);
    if (dir == NULL) {
        pthread_mutex_lock(&lock);
        printf("[нет доступа] %s\n", task->path);
        errors++;
        pthread_mutex_unlock(&lock);
        return;
    }

    long long files = 0;
    long long size = 0;
    struct dirent *entry;
    char full[MAX_PATH];
    struct stat st;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full, sizeof(full), "%s/%s", task->path, entry->d_name);

        if (lstat(full, &st) != 0) {
            pthread_mutex_lock(&lock);
            errors++;
            pthread_mutex_unlock(&lock);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (task->depth < max_depth) {
                pthread_mutex_lock(&lock);
                push_task(full, task->depth + 1);
                pthread_mutex_unlock(&lock);
            }
        } else if (S_ISREG(st.st_mode)) {
            files++;
            size += st.st_size;
            pthread_mutex_lock(&lock);
            add_to_top(full, st.st_size);
            pthread_mutex_unlock(&lock);
        }
    }
    closedir(dir);

    pthread_mutex_lock(&lock);
    total_files += files;
    total_size += size;
    total_dirs++;
    printf("%8lld файлов  %s\n", files, task->path);
    pthread_mutex_unlock(&lock);
}

void *worker(void *arg)
{
    struct Task task;
    while (1) {
        pthread_mutex_lock(&lock);
        if (queue_size == 0 && busy == 0) {
            pthread_mutex_unlock(&lock);
            break;
        }
        if (queue_size == 0) {
            pthread_mutex_unlock(&lock);
            usleep(100);
            continue;
        }
        queue_size--;
        task = queue[queue_size];
        busy++;
        pthread_mutex_unlock(&lock);

        scan_dir(&task);

        pthread_mutex_lock(&lock);
        busy--;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int compare_top(const void *a, const void *b)
{
    long long x = ((struct BigFile *)a)->size;
    long long y = ((struct BigFile *)b)->size;
    if (x < y) return 1;
    if (x > y) return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    char *start_dir = ".";
    int threads = 4;

    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--dir") == 0) start_dir = argv[i + 1];
        else if (strcmp(argv[i], "--threads") == 0) threads = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--depth") == 0) max_depth = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--top") == 0) top_n = atoi(argv[i + 1]);
    }
    if (threads < 1) threads = 1;
    if (top_n < 0) top_n = 0;
    if (top_n > MAX_TOP) top_n = MAX_TOP;

    printf("Папка: %s, потоков: %d, глубина: %d, топ: %d\n\n",
           start_dir, threads, max_depth, top_n);

    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    push_task(start_dir, 0);

    pthread_t *ids = malloc(threads * sizeof(pthread_t));
    for (int i = 0; i < threads; i++)
        pthread_create(&ids[i], NULL, worker, NULL);
    for (int i = 0; i < threads; i++)
        pthread_join(ids[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t2);
    double seconds = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;

    printf("\nИтого:\n");
    printf("  папок:          %lld\n", total_dirs);
    printf("  файлов:         %lld\n", total_files);
    printf("  общий размер:   %lld байт (%.2f МБ)\n", total_size, total_size / 1048576.0);
    printf("  ошибок доступа: %lld\n", errors);

    qsort(top, top_count, sizeof(struct BigFile), compare_top);
    printf("\nТоп-%d самых больших файлов:\n", top_n);
    for (int i = 0; i < top_count; i++)
        printf("  %2d. %12lld байт  %s\n", i + 1, top[i].size, top[i].path);

    printf("\nВремя: %.3f с\n", seconds);
    printf("Скорость: %.0f файлов/с\n", total_files / seconds);

    free(ids);
    free(queue);
    return 0;
}
