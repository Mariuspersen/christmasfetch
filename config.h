#include "logos/arch.h"
#define COLOR "\e[1;36m"

#define CONFIG \
{ \
   /* name            function                 cached */\
    { "",             get_title,            false   },  \
    { "",             get_bar,              false   },  \
    { "Xmas in: ",    get_christmas,        false   },  \
    { "OS: ",         get_os,               false   },  \
    { "Host: ",       get_host,             false   },  \
    { "Kernel: ",     get_kernel,           false   },  \
    { "Uptime: ",     get_uptime,           false   },  \
    { "Packages: ",   get_packages_pacman,  false   },  \
    { "Shell: ",      get_shell,            false   },  \
    { "Resolution: ", get_resolution,       false   },  \
    { "Terminal: ",   get_terminal,         false   },  \
    { "CPU: ",        get_cpu,              false   },  \
    { "GPU: ",        get_gpu1,             false   },  \
    { "Memory: ",     get_memory,           false   },  \
    { "",             get_colors1,          false   },  \
    { "",             get_colors2,          false   },  \
}

#define CPU_CONFIG \
{ \
   REMOVE("(R)"), \
   REMOVE("(TM)"), \
   REMOVE("Dual-Core"), \
   REMOVE("Quad-Core"), \
   REMOVE("Six-Core"), \
   REMOVE("Eight-Core"), \
   REMOVE("Core"), \
   REMOVE("CPU"), \
}

#define GPU_CONFIG \
{ \
    REMOVE("Corporation"), \
}
