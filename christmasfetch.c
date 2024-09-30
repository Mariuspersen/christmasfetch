#include <stddef.h>
#include <sys/types.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define UNINSTALL_REG_KEY_32 "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
#define UNINSTALL_REG_KEY_64 "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
#define SEP "\\"
#elif __linux__
#define SEP "/"
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>

#include <pci/pci.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

Display *display;
struct statvfs file_stats;
struct utsname uname_info;
struct sysinfo my_sysinfo;
#endif

#include "christmasfetch.h"
#include "config.h"

#define BUF_SIZE 150
#define COUNT(x) (int)(sizeof x / sizeof *x)

#define halt_and_catch_fire(fmt, ...)                                \
    do                                                               \
    {                                                                \
        if (status != 0)                                             \
        {                                                            \
            fprintf(stderr, "paleofetch: " fmt "\n", ##__VA_ARGS__); \
            exit(status);                                            \
        }                                                            \
    } while (0)

struct conf
{
    char *label, *(*function)();
    bool cached;
} config[] = CONFIG;

struct
{
    char *substring;
    char *repl_str;
    size_t length;
    size_t repl_len;
} cpu_config[] = CPU_CONFIG, gpu_config[] = GPU_CONFIG;

int title_length, status;

/*
 * Replaces the first newline character with null terminator
 */
void remove_newline(char *s)
{
    while (*s != '\0' && *s != '\n')
        s++;
    *s = '\0';
}

/*
 * Replaces the first newline character with null terminator
 * and returns the length of the string
 */
int remove_newline_get_length(char *s)
{
    int i;
    for (i = 0; *s != '\0' && *s != '\n'; s++, i++)
        ;
    *s = '\0';
    return i;
}

/*
 * Cleans up repeated spaces in a string
 * Trim spaces at the front of a string
 */
void truncate_spaces(char *str)
{
    int src = 0, dst = 0;
    while (*(str + dst) == ' ')
        dst++;

    while (*(str + dst) != '\0')
    {
        *(str + src) = *(str + dst);
        if (*(str + (dst++)) == ' ')
            while (*(str + dst) == ' ')
                dst++;

        src++;
    }

    *(str + src) = '\0';
}

/*
 * Removes the first len characters of substring from str
 * Assumes that strlen(substring) >= len
 * Returns index where substring was found, or -1 if substring isn't found
 */
void remove_substring(char *str, const char *substring, size_t len)
{
    /* shift over the rest of the string to remove substring */
    char *sub = strstr(str, substring);
    if (sub == NULL)
        return;

    int i = 0;
    do
        *(sub + i) = *(sub + i + len);
    while (*(sub + (++i)) != '\0');
}

/*
 * Replaces the first sub_len characters of sub_str from str
 * with the first repl_len characters of repl_str
 */
void replace_substring(char *str, const char *sub_str, const char *repl_str, size_t sub_len, size_t repl_len)
{
    char buffer[BUF_SIZE / 2];
    char *start = strstr(str, sub_str);
    if (start == NULL)
        return; // substring not found

    /* check if we have enough space for new substring */
    if (strlen(str) - sub_len + repl_len >= BUF_SIZE / 2)
    {
        status = -1;
        halt_and_catch_fire("new substring too long to replace");
    }

    strcpy(buffer, start + sub_len);
    strncpy(start, repl_str, repl_len);
    strcpy(start + repl_len, buffer);
}

static char *get_title()
{
    char hostname[BUF_SIZE / 3];
#ifdef _WIN32
    DWORD hostname_size = sizeof(hostname);
    GetComputerName(hostname, &hostname_size);
#elif __linux__
    status = gethostname(hostname, BUF_SIZE / 3);
    halt_and_catch_fire("unable to retrieve host name");
#endif
    // reduce the maximum size for these, so that we don't over-fill the title string

    char username[BUF_SIZE / 3];
    DWORD username_size = sizeof(username);
    status = GetUserName(username, &username_size);
    // halt_and_catch_fire("unable to retrieve login name");

    title_length = strlen(hostname) + strlen(username) + 1;

    char *title = malloc(BUF_SIZE);
    snprintf(title, BUF_SIZE, COLOR "%s\e[0m@" COLOR "%s", username, hostname);

    return title;
}

static char *get_bar()
{
    char *bar = malloc(BUF_SIZE);
    char *s = bar;
    for (int i = 0; i < title_length; i++)
        *(s++) = '-';
    *s = '\0';
    return bar;
}

static char *get_os()
{
    char *os = malloc(BUF_SIZE);
#ifdef __linux__
    char *name = malloc(BUF_SIZE),
         char *line = NULL;
    size_t len;
    FILE *os_release = fopen("/etc/os-release", "r");
    if (os_release == NULL)
    {
        status = -1;
        halt_and_catch_fire("unable to open /etc/os-release");
    }

    while (getline(&line, &len, os_release) != -1)
    {
        if (sscanf(line, "NAME=\"%[^\"]+", name) > 0)
            break;
    }

    free(line);
    fclose(os_release);
    snprintf(os, BUF_SIZE, "%s %s", name, uname_info.machine);
    free(name);
#elif _WIN32
    OSVERSIONINFOA osinfo = { 0 };
    osinfo.dwOSVersionInfoSize = sizeof(osinfo);
    
    if(GetVersionExA(&osinfo)) {
        snprintf(os, BUF_SIZE, "Windows %lu.%lu (%lu) %s",osinfo.dwMajorVersion, osinfo.dwMinorVersion, osinfo.dwBuildNumber,osinfo.szCSDVersion);
    }


#endif
    return os;
}

static char *get_kernel()
{
    char *kernel = malloc(BUF_SIZE);
#ifdef __linux__
    strncpy(kernel, uname_info.release, BUF_SIZE);
#elif _WIN32
    char* env = getenv("OS");
    snprintf(kernel, BUF_SIZE, "%s",env);
#endif
    return kernel;
}

static char *get_host()
{
    char *host = malloc(BUF_SIZE), buffer[BUF_SIZE / 2];
#ifdef __linux__
    FILE *product_name, *product_version, *model;

    if ((product_name = fopen("/sys/devices/virtual/dmi/id/product_name", "r")) != NULL)
    {
        if ((product_version = fopen("/sys/devices/virtual/dmi/id/product_version", "r")) != NULL)
        {
            fread(host, 1, BUF_SIZE / 2, product_name);
            remove_newline(host);
            strcat(host, " ");
            fread(buffer, 1, BUF_SIZE / 2, product_version);
            remove_newline(buffer);
            strcmp(buffer, "Default string") ? strcat(host, buffer) : strcat(host, "");
            fclose(product_version);
        }
        else
        {
            fclose(product_name);
            goto model_fallback;
        }
        fclose(product_name);
        return host;
    }

model_fallback:
    if ((model = fopen("/sys/firmware/devicetree/base/model", "r")) != NULL)
    {
        fread(host, 1, BUF_SIZE, model);
        remove_newline(host);
        return host;
    }

    status = -1;
    halt_and_catch_fire("unable to get host");
    return NULL;
#elif _WIN32
    DWORD size = sizeof(buffer);
    GetComputerName(buffer, &size);
    snprintf(host,BUF_SIZE,"%s",buffer);
    return host;
#endif
}

static char *get_uptime()
{
    char *uptime = malloc(BUF_SIZE);
#ifdef __linux__
    long seconds = my_sysinfo.uptime;
    struct
    {
        char *name;
        int secs;
    } units[] = {
        {"day", 60 * 60 * 24},
        {"hour", 60 * 60},
        {"min", 60},
    };

    int n, len = 0;
    for (int i = 0; i < 3; ++i)
    {
        if ((n = seconds / units[i].secs) || i == 2) /* always print minutes */
            len += snprintf(uptime + len, BUF_SIZE - len,
                            "%d %s%s, ", n, units[i].name, n != 1 ? "s" : "");
        seconds %= units[i].secs;
    }

    // null-terminate at the trailing comma
    uptime[len - 2] = '\0';
#elif _WIN32
    ULONGLONG ticks = GetTickCount64();
    ULONGLONG seconds = ticks / 1000;
    ULONGLONG minutes = seconds / 60;
    ULONGLONG hours = minutes / 60;
    ULONGLONG days = hours / 24;

    seconds %= 60;
    minutes %= 60;
    hours %= 24;

    snprintf(uptime, BUF_SIZE, "%llu days, %llu hours, %llu minutes, %llu seconds",
             days, hours, minutes, seconds);
#endif
    return uptime;
}

// returns "<Battery Percentage>% [<Charging | Discharging | Unknown>]"
// Credit: allisio - https://gist.github.com/allisio/1e850b93c81150124c2634716fbc4815
static char *get_battery_percentage()
{
    char *battery = malloc(20);
#ifdef BATTERY_DIRECTORY
    int battery_capacity;
    FILE *capacity_file, *status_file;
    char battery_status[12] = "Unknown";
    if ((capacity_file = fopen(BATTERY_DIRECTORY "/capacity", "r")) == NULL)
    {
        status = ENOENT;
        halt_and_catch_fire("Unable to get battery information");
    }

    fscanf(capacity_file, "%d", &battery_capacity);
    fclose(capacity_file);

    if ((status_file = fopen(BATTERY_DIRECTORY "/status", "r")) != NULL)
    {
        fscanf(status_file, "%s", battery_status);
        fclose(status_file);
    }

    // max length of resulting string is 19
    // one byte for padding incase there is a newline
    // 100% [Discharging]
    // 1234567890123456789

    snprintf(battery, 20, "%d%% [%s]", battery_capacity, battery_status);
#elif _WIN32
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status) && status.BatteryFlag != 128)
    {
        snprintf(battery, 20, "%u%%", status.BatteryLifePercent);
    }
#endif
    return battery;
}

static char *get_packages(const char *dirname, const char *pacname, int num_extraneous)
{
    int num_packages = 0;
    char *packages = malloc(BUF_SIZE);
#ifdef __linux__
    DIR *dirp;
    struct dirent *entry;

    dirp = opendir(dirname);

    if (dirp == NULL)
    {
        status = -1;
        halt_and_catch_fire("You may not have %s installed", dirname);
    }

    while ((entry = readdir(dirp)) != NULL)
    {
        if (entry->d_type == DT_DIR)
            num_packages++;
    }
    num_packages -= (2 + num_extraneous); // accounting for . and ..

    status = closedir(dirp);

    snprintf(packages, BUF_SIZE, "%d (%s)", num_packages, pacname);
#elif _WIN32
    (void)dirname;
    (void)pacname;
    (void)num_extraneous;
    HKEY hKey;
    DWORD count = 0;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, UNINSTALL_REG_KEY_32, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryInfoKey(hKey, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        RegCloseKey(hKey);
        num_packages += count;
    }
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, UNINSTALL_REG_KEY_64, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        count = 0;
        RegQueryInfoKey(hKey, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        RegCloseKey(hKey);
        num_packages += count;
    }
    if (RegOpenKeyEx(HKEY_CURRENT_USER, UNINSTALL_REG_KEY_32, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        count = 0;
        RegQueryInfoKey(hKey, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        RegCloseKey(hKey);
        num_packages += count;
    }
    if (RegOpenKeyEx(HKEY_CURRENT_USER, UNINSTALL_REG_KEY_64, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        count = 0;
        RegQueryInfoKey(hKey, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        RegCloseKey(hKey);
        num_packages += count;
    }
    snprintf(packages, BUF_SIZE, "%d", num_packages);

#endif
    return packages;
}

static char *get_packages_pacman()
{
    return get_packages("/var/lib/pacman/local", "pacman", 0);
}

static char *get_shell()
{
    char *shell = malloc(BUF_SIZE);
    char *shell_path;
#ifdef __linux__
    shell_path = getenv("SHELL");
    char *shell_name = strrchr(getenv("SHELL"), '/');

    if (shell_name == NULL)                   /* if $SHELL doesn't have a '/' */
        strncpy(shell, shell_path, BUF_SIZE); /* copy the whole thing over */
    else
        strncpy(shell, shell_name + 1, BUF_SIZE); /* o/w copy past the last '/' */
#elif _WIN32
    shell_path = getenv("SHELL");
    snprintf(shell,BUF_SIZE,"%s",shell_path);
#endif

    return shell;
}

static char *get_resolution()
{
    int screen, width, height;
    char *resolution = malloc(BUF_SIZE);
#ifdef __linux__

    if (display != NULL)
    {
        screen = DefaultScreen(display);

        width = DisplayWidth(display, screen);
        height = DisplayHeight(display, screen);

        snprintf(resolution, BUF_SIZE, "%dx%d", width, height);
    }
    else
    {
        DIR *dir;
        struct dirent *entry;
        char dir_name[] = "/sys/class/drm";
        char modes_file_name[BUF_SIZE * 2];
        FILE *modes;
        char *line = NULL;
        size_t len;

        /* preload resolution with empty string, in case we cant find a resolution through parsing */
        strncpy(resolution, "", BUF_SIZE);

        dir = opendir(dir_name);
        if (dir == NULL)
        {
            status = -1;
            halt_and_catch_fire("Could not open /sys/class/drm to determine resolution in tty mode.");
        }
        /* parse through all directories and look for a non empty modes file */
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_type == DT_LNK)
            {
                snprintf(modes_file_name, BUF_SIZE * 2, "%s/%s/modes", dir_name, entry->d_name);

                modes = fopen(modes_file_name, "r");
                if (modes != NULL)
                {
                    if (getline(&line, &len, modes) != -1)
                    {
                        strncpy(resolution, line, BUF_SIZE);
                        remove_newline(resolution);

                        free(line);
                        fclose(modes);

                        break;
                    }

                    fclose(modes);
                }
            }
        }

        closedir(dir);
    }
#elif _WIN32
    (void)screen;
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    snprintf(resolution, BUF_SIZE, "%dx%d", width, height);
#endif
    return resolution;
}

static char *get_terminal()
{
    char *terminal = malloc(BUF_SIZE);
#ifdef __linux__
    unsigned char *prop;
    /* check if xserver is running or if we are running in a straight tty */
    if (display != NULL)
    {

        unsigned long _, // not unused, but we don't need the results
            window = RootWindow(display, XDefaultScreen(display));
        Atom a,
            active = XInternAtom(display, "_NET_ACTIVE_WINDOW", True),
            class = XInternAtom(display, "WM_CLASS", True);

#define GetProp(property) \
    XGetWindowProperty(display, window, property, 0, 64, 0, 0, &a, (int *)&_, &_, &_, &prop);

        GetProp(active);
        window = (prop[3] << 24) + (prop[2] << 16) + (prop[1] << 8) + prop[0];
        free(prop);
        if (!window)
            goto terminal_fallback;
        GetProp(class);

#undef GetProp

        snprintf(terminal, BUF_SIZE, "%s", prop);
        free(prop);
    }
    else
    {
    terminal_fallback:
        strncpy(terminal, getenv("TERM"), BUF_SIZE); /* fallback to old method */
        /* in tty, $TERM is simply returned as "linux"; in this case get actual tty name */
        if (strcmp(terminal, "linux") == 0)
        {
            strncpy(terminal, ttyname(STDIN_FILENO), BUF_SIZE);
        }
    }
#elif _WIN32
    const char* comspec = getenv("ComSpec");
    snprintf(terminal, BUF_SIZE, "%s",comspec);
#endif
    return terminal;
}

static char *get_cpu()
{
    char *cpu = malloc(BUF_SIZE);
#ifdef __linux__
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r"); /* read from cpu info */
    if (cpuinfo == NULL)
    {
        status = -1;
        halt_and_catch_fire("Unable to open cpuinfo");
    }

    char *cpu_model = malloc(BUF_SIZE / 2);
    char *line = NULL;
    size_t len; /* unused */
    int num_cores = 0, cpu_freq, prec = 3;
    double freq;
    char freq_unit[] = "GHz";

    /* read the model name into cpu_model, and increment num_cores every time model name is found */
    while (getline(&line, &len, cpuinfo) != -1)
    {
        num_cores += sscanf(line, "model name	: %[^\n@]", cpu_model);
    }
    free(line);
    fclose(cpuinfo);

    FILE *cpufreq = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    line = NULL;

    if (cpufreq != NULL)
    {
        if (getline(&line, &len, cpufreq) != -1)
        {
            sscanf(line, "%d", &cpu_freq);
            cpu_freq /= 1000; // convert kHz to MHz
        }
        else
        {
            fclose(cpufreq);
            free(line);
            goto cpufreq_fallback;
        }
    }
    else
    {
    cpufreq_fallback:
        cpufreq = fopen("/proc/cpuinfo", "r"); /* read from cpu info */
        if (cpufreq == NULL)
        {
            status = -1;
            halt_and_catch_fire("Unable to open cpuinfo");
        }

        while (getline(&line, &len, cpufreq) != -1)
        {
            if (sscanf(line, "cpu MHz : %lf", &freq) > 0)
                break;
        }

        cpu_freq = (int)freq;
    }

    free(line);
    fclose(cpufreq);

    if (cpu_freq < 1000)
    {
        freq = (double)cpu_freq;
        freq_unit[0] = 'M'; // make MHz from GHz
        prec = 0;           // show frequency as integer value
    }
    else
    {
        freq = cpu_freq / 1000.0; // convert MHz to GHz and cast to double

        while (cpu_freq % 10 == 0)
        {
            --prec;
            cpu_freq /= 10;
        }

        if (prec == 0)
            prec = 1; // we don't want zero decimal places
    }

    /* remove unneeded information */
    for (int i = 0; i < COUNT(cpu_config); ++i)
    {
        if (cpu_config[i].repl_str == NULL)
        {
            remove_substring(cpu_model, cpu_config[i].substring, cpu_config[i].length);
        }
        else
        {
            replace_substring(cpu_model, cpu_config[i].substring, cpu_config[i].repl_str, cpu_config[i].length, cpu_config[i].repl_len);
        }
    }

    snprintf(cpu, BUF_SIZE, "%s (%d) @ %.*f%s", cpu_model, num_cores, prec, freq, freq_unit);
    free(cpu_model);

    truncate_spaces(cpu);

    if (num_cores == 0)
        *cpu = '\0';
#elif _WIN32
    SYSTEM_INFO sysinfo = {0};
    GetSystemInfo(&sysinfo);

    snprintf(cpu, BUF_SIZE, "%u(%lu)", sysinfo.wProcessorArchitecture, sysinfo.dwNumberOfProcessors);
#endif
    return cpu;
}

static char *find_gpu(int index)
{
    char *gpu = malloc(BUF_SIZE);
#ifdef __linux__
    // inspired by https://github.com/pciutils/pciutils/edit/master/example.c
    /* it seems that pci_lookup_name needs to be given a buffer, but I can't for the life of my figure out what its for */
    char buffer[BUF_SIZE], *device_class;
    struct pci_access *pacc;
    struct pci_dev *dev;
    int gpu_index = 0;
    bool found = false;

    pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);
    dev = pacc->devices;

    while (dev != NULL)
    {
        pci_fill_info(dev, PCI_FILL_IDENT);
        device_class = pci_lookup_name(pacc, buffer, sizeof(buffer), PCI_LOOKUP_CLASS, dev->device_class);
        if (strcmp("VGA compatible controller", device_class) == 0 || strcmp("3D controller", device_class) == 0)
        {
            strncpy(gpu, pci_lookup_name(pacc, buffer, sizeof(buffer), PCI_LOOKUP_DEVICE | PCI_LOOKUP_VENDOR, dev->vendor_id, dev->device_id), BUF_SIZE);
            if (gpu_index == index)
            {
                found = true;
                break;
            }
            else
            {
                gpu_index++;
            }
        }

        dev = dev->next;
    }

    if (found == false)
        *gpu = '\0'; // empty string, so it will not be printed

    pci_cleanup(pacc);

    /* remove unneeded information */
    for (int i = 0; i < COUNT(gpu_config); ++i)
    {
        if (gpu_config[i].repl_str == NULL)
        {
            remove_substring(gpu, gpu_config[i].substring, gpu_config[i].length);
        }
        else
        {
            replace_substring(gpu, gpu_config[i].substring, gpu_config[i].repl_str, gpu_config[i].length, gpu_config[i].repl_len);
        }
    }

    truncate_spaces(gpu);
#elif _WIN32
    (void)index;
    snprintf(gpu, BUF_SIZE, "WINDOWS: NOT IMPLEMENTED");
#endif
    return gpu;
}

static char *get_gpu1()
{
    return find_gpu(0);
}

static char *get_gpu2()
{
    return find_gpu(1);
}

static char *get_memory()
{
    char *memory = malloc(BUF_SIZE);
    int total_memory = 0, used_memory = 0;
#ifdef __linux__
    int total, shared, memfree, buffers, cached, reclaimable;

    FILE *meminfo = fopen("/proc/meminfo", "r"); /* get infomation from meminfo */
    if (meminfo == NULL)
    {
        status = -1;
        halt_and_catch_fire("Unable to open meminfo");
    }

    /* We parse through all lines of meminfo and scan for the information we need */
    char *line = NULL; // allocation handled automatically by getline()
    size_t len;        /* unused */

    /* parse until EOF */
    while (getline(&line, &len, meminfo) != -1)
    {
        /* if sscanf doesn't find a match, pointer is untouched */
        sscanf(line, "MemTotal: %d", &total);
        sscanf(line, "Shmem: %d", &shared);
        sscanf(line, "MemFree: %d", &memfree);
        sscanf(line, "Buffers: %d", &buffers);
        sscanf(line, "Cached: %d", &cached);
        sscanf(line, "SReclaimable: %d", &reclaimable);
    }

    free(line);

    fclose(meminfo);

    /* use same calculation as neofetch */
    used_memory = (total + shared - memfree - buffers - cached - reclaimable) / 1024;
    total_memory = total / 1024;
    int percentage = (int)(100 * (used_memory / (double)total_memory));
#elif _WIN32
    MEMORYSTATUSEX memstat = {0};
    memstat.dwLength = sizeof(memstat);
    int percentage = 0;
    if (GlobalMemoryStatusEx(&memstat))
    {
        total_memory = memstat.ullTotalPhys / (1024 * 1024);
        used_memory = (memstat.ullTotalPhys - memstat.ullAvailPhys) / (1024 * 1024);
        percentage = (int)((double)100 * ((double)used_memory / (double)total_memory));
    }
#endif
    snprintf(memory, BUF_SIZE, "%dMB / %dMB (%d%%)", used_memory, total_memory, percentage);
    return memory;
}

static char *get_disk_usage(const char *folder)
{
    char *disk_usage = malloc(BUF_SIZE);
#ifdef __linux
    long total, used, free;
    int percentage;
    status = statvfs(folder, &file_stats);
    halt_and_catch_fire("Error getting disk usage for %s", folder);
    total = file_stats.f_blocks * file_stats.f_frsize;
    free = file_stats.f_bfree * file_stats.f_frsize;
    used = total - free;
    percentage = (used / (double)total) * 100;
#define TO_GB(A) ((A) / (1024.0 * 1024 * 1024))
    snprintf(disk_usage, BUF_SIZE, "%.1fGiB / %.1fGiB (%d%%)", TO_GB(used), TO_GB(total), percentage);
#undef TO_GB
#elif _WIN32
    (void)folder;
    snprintf(disk_usage, BUF_SIZE, "WINDOWS: NOT IMPLEMENTED");
#endif
    return disk_usage;
}

static char *get_disk_usage_root()
{
    return get_disk_usage("/");
}

static char *get_disk_usage_home()
{
    return get_disk_usage("/home");
}

static char *get_colors1()
{
    char *colors1 = malloc(BUF_SIZE);
    char *s = colors1;

    for (int i = 0; i < 8; i++)
    {
        sprintf(s, "\e[4%dm   ", i);
        s += 8;
    }
    snprintf(s, 5, "\e[0m");

    return colors1;
}

static char *get_colors2()
{
    char *colors2 = malloc(BUF_SIZE);
    char *s = colors2;

    for (int i = 8; i < 16; i++)
    {
        sprintf(s, "\e[48;5;%dm   ", i);
        s += 12 + (i >= 10 ? 1 : 0);
    }
    snprintf(s, 5, "\e[0m");

    return colors2;
}

static int getDayoftheYear(unsigned int year, unsigned int month, unsigned int day)
{
    int daysinamonth[2] = {31, 30};
    unsigned int days_in_feb = 28;
    unsigned int totaldays = 1;
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
    {
        days_in_feb = 29;
    }
    // Handling special cases
    if (month == 1)
        return day;

    if (month == 2)
    {
        return daysinamonth[0] + day;
    }
    // General case
    totaldays += day;
    totaldays += days_in_feb;
    for (size_t i = 0; i < month - 2; i++)
    {
        totaldays += daysinamonth[i % 2];
    }
    return totaldays;
}

static char *get_christmas()
{
    char *daystochristmas = malloc(sizeof(char) * 40);
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(daystochristmas, "%d Days!", getDayoftheYear(tm.tm_year + 1900, 12, 24) - getDayoftheYear(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday));
    return daystochristmas;
}

static char *spacer()
{
    return calloc(1, 1); // freeable, null-terminated string of length 1
}

char *get_cache_file()
{
    char *cache_file = malloc(BUF_SIZE);
    char *env = NULL;
#ifdef __linux__

    env = getenv("XDG_CACHE_HOME");
#elif _WIN32
    env = getenv("LOCALAPPDATA");
#endif
    if (env == NULL)
    {
        char *homedir = NULL;
#ifdef __linux
        homedir = getenv("HOME");
#elif _WIN32
        homedir = getenv("USERPROFILE");
#endif
        snprintf(cache_file, BUF_SIZE, "%s" SEP ".cache" SEP "paleofetch", homedir);
    }
    else
        snprintf(cache_file, BUF_SIZE, "%s" SEP "paleofetch", env);

    return cache_file;
}

/* This isn't especially robust, but as long as we're the only one writing
 * to our cache file, the format is simple, effective, and fast. One way
 * we might get in trouble would be if the user decided not to have any
 * sort of sigil (like ':') after their labels. */
char *search_cache(char *cache_data, char *label)
{
    if (!cache_data)
    {
        halt_and_catch_fire("cache data null");
    }
    
    char *start = strstr(cache_data, label);
    if (start == NULL)
    {
        status = ENODATA;
        halt_and_catch_fire("cache miss on key '%s'; need to --recache?", label);
    }
    start += strlen(label);
    char *end = strchr(start, ';');
    char *buf = calloc(1, BUF_SIZE);
    // skip past the '=' and stop just before the ';'
    strncpy(buf, start + 1, end - start - 1);

    return buf;
}

char *get_value(struct conf c, int read_cache, char *cache_data)
{
    char *value;
#ifdef __linux__
    // If the user's config specifies that this value should be cached
    if (c.cached && read_cache)                    // and we have a cache to read from
        value = search_cache(cache_data, c.label); // grab it from the cache
    else
    {
        // Otherwise, call the associated function to get the value
        value = c.function();
        if (c.cached)
        { // and append it to our cache data if appropriate
            char *buf = malloc(BUF_SIZE);
            sprintf(buf, "%s=%s;", c.label, value);
            strcat(cache_data, buf);
            free(buf);
        }
    }
#elif _WIN32
#endif
    (void)read_cache;
    (void)cache_data;
    value = c.function();
    return value;
}

int main(int argc, char *argv[])
{
    char *cache, *cache_data = NULL;
    int read_cache;
    FILE *cache_file;
    cache = get_cache_file();
    if (argc == 2 && strcmp(argv[1], "--recache") == 0)
        read_cache = 0;
    else
    {
        cache_file = fopen(cache, "r");
        read_cache = cache_file != NULL;
        if (!read_cache)
            cache_data = calloc(4, BUF_SIZE); // should be enough
        else
        {
#ifdef __linux__            
            size_t len; /* unused */
            getline(&cache_data, &len, cache_file);
#endif
            fclose(cache_file); // We just need the first (and only) line.
        }
    }
#ifdef __linux__

    status = uname(&uname_info);
    halt_and_catch_fire("uname failed");
    status = sysinfo(&my_sysinfo);
    halt_and_catch_fire("sysinfo failed");
    display = XOpenDisplay(NULL);

#endif
    int offset = 0;

    for (int i = 0; i < COUNT(LOGO); i++)
    {
        // If we've run out of information to show...
        if (i >= COUNT(config) - offset) // just print the next line of the logo
            printf(COLOR "%s\n", LOGO[i]);
        else
        {
            // Otherwise, we've got a bit of work to do.
            char *label = config[i + offset].label,
                 *value = get_value(config[i + offset], read_cache, cache_data);
            if (strcmp(value, "") != 0)
            {                                                         // check if value is an empty string
                printf(COLOR "%s%s\e[0m%s\n", LOGO[i], label, value); // just print if not empty
            }
            else
            {
                if (strcmp(label, "") != 0)
                {                                     // check if label is empty, otherwise it's a spacer
                    ++offset;                         // print next line of information
                    free(value);                      // free memory allocated for empty value
                    label = config[i + offset].label; // read new label and value
                    value = get_value(config[i + offset], read_cache, cache_data);
                }
                printf(COLOR "%s%s\e[0m%s\n", LOGO[i], label, value);
            }
            free(value);
        }
    }
    puts("\e[0m");

    /* Write out our cache data (if we have any). */
    if (!read_cache && *cache_data)
    {
        cache_file = fopen(cache, "w");
        fprintf(cache_file, "%s", cache_data);
        fclose(cache_file);
    }
#ifdef __linux__
    free(cache);
    free(cache_data);
    if (display != NULL)
    {
        XCloseDisplay(display);
    }
#endif
    return 0;
}
