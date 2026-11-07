Компиляция:
gcc -O2 -pthread -o disk_scanner disk_scanner.c

Пример запуска:
./dir_scanner --dir /home/ --threads 4 --depth 3 --top 10
