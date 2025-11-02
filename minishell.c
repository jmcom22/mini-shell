/*
Ahora en este paso se implementan las estadísticas
*/

/*
Con esta macro se asegura que el compilador dé acceso a las
funciones y declaraciones del estándar POSIX.1-2008 necesarias
para el código, como las funciones de sistema en <unistd.h> y
el manejo avanzado de señales.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>

#define MAX_LINE 1024
#define MAX_ARGS 64 

static void perr(const char *msg)
{
    fprintf(stderr, "Error: %s: %s\n", msg, strerror(errno));
}

static void trim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) s[--len] = '\0';
}

/*
Función para convertir números enteros a un string en formato
decimal, es imprescindible para convertir enteros (como el PID
y el estado) a una cadena decimal de forma async-signal-safe,
permitiendo que el manejador de la señal SIGCHLD reporte la
terminación de los procesos en background sin usar funciones
inseguras como sprintf
*/
static int int_to_dec_str(int num, char *buf, size_t bufsize)
{
    if (bufsize==0) return 0;
    if (num==0)
    {
        if (bufsize<2) return 0;
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    unsigned int n = (unsigned int)(num<0? -num : num);
    char tmp[32];
    int i = 0;
    while (n && i<(int)sizeof(tmp))
    {
        tmp[i++] = '0' + (n%10);
        n/=10;
    }

    int p = 0;
    if (num<0)
    {
        if (p+1>=(int)bufsize) return 0;
        buf[p++]='-';
    }
    while (i>0)
    {
        if (p+1>=(int)bufsize) return 0;
        buf[p++] = tmp[i--];
    }
    if (p>=(int)bufsize) return 0;
    buf[p] = '\0';
    return p;
}

#ifdef SIGNALDETECTION
// Manejador de SIGCHLD
static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        char buf[200];
        const char *pre = "Proceso en background con PID ";
        const char *mid = " termino (status ";
        const char *post = ")\n";
        size_t off = 0;

        size_t lpre = strlen(pre);
        if (off + lpre < sizeof(buf))
        {
            memcpy(buf + off, pre, lpre);
            off += lpre;
        }

        char numbuf[32];
        int n = int_to_dec_str((int)pid, numbuf, sizeof(numbuf));
        if (n > 0 && off + (size_t)n < sizeof(buf))
        {
            memcpy(buf + off, numbuf, (size_t)n);
            off += (size_t)n;
        }

        size_t lmid = strlen(mid);
        if (off + lmid < sizeof(buf))
        {
            memcpy(buf + off, mid, lmid);
            off += lmid;
        }

        n = int_to_dec_str(status, numbuf, sizeof(numbuf));
        if (n > 0 && off + (size_t)n < sizeof(buf))
        {
            memcpy(buf + off, numbuf, (size_t)n);
            off += (size_t)n;
        }

        size_t lpost = strlen(post);
        if (off + lpost < sizeof(buf))
        {
            memcpy(buf + off, post, lpost);
            off += lpost;
        }

        /* write es async-signal-safe */
        ssize_t wr = write(STDOUT_FILENO, buf, off);
        (void)wr;
    }
    errno = saved_errno;
}
#endif

//Función para revisión por sondeo
static void reap_background_polling(void)
{
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        printf("Proceso en background con PID %d termino (status %d)\n", (int)pid, status);
    }
}

int main(void)
{
    char line[MAX_LINE];  // Línea de comando
    char *argv[MAX_ARGS];
    char *home = getenv("HOME"); //path al home

    // Esto para que cuando se compile con SIGNALDETECTION, se empiece a enviar el SIGCHLD cuando el padre esté escuchando
    sigset_t block_mask, prev_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);

    // Para evitar el ctrl-c mate al shell (porque solo se puede salir con exit)
    if (signal(SIGINT, SIG_IGN) == SIG_ERR)
    {
        perr("signal(SIGINT)");
    }

#ifdef SIGNALDETECTION
    //Se instala el SIGCHLD handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perr("sigaction(SIGCHLD)");
    }
#endif

    while (1)
    {

#ifndef SIGNALDETECTION
        // Si no se declara que la búsqueda de hijos se hará por señales, entonces se hace por sondeo
        reap_background_polling();
#endif

        printf("mini-shell$ "); //Cambiar esto luego para que se muestre el directorio
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            if (feof(stdin))
            {
                printf("\n"); // comportamiento agradable al recibir EOF
                break;
            }
            else
            {
                if (errno == EINTR)
                { // lectura interrumpida por señal
                    clearerr(stdin);
                    continue;
                }
                perr("fgets");
                continue;
            }
        }

        // quitar '\n'
        size_t ln = strlen(line);
        if (ln > 0 && line[ln-1] == '\n') line[ln-1] = '\0';

        trim(line);
        if (line[0] == '\0') continue; // si se recibe línea vacía, entonces nuevo prompt

        if (strlen(line) > MAX_LINE-2) // -2 para no contar el null y el salto de línea
        {
            fprintf(stderr, "Input demasiado largo (max %d caracteres)\n", MAX_LINE-2);
            continue;
        }

        //Detectar si está el marcador & para que el proceso se haga en el backgroudn
        int background = 0;
        size_t l = strlen(line);
        if (l>0)
        {
            size_t i = l;
            while (i > 0 && (line[i-1] == ' ' || line[i-1] == '\t')) i--; //Esto para obtener el último caracter que no sea un espacio
            if (i > 0 && line[i-1] == '&') {
                background = 1;
                // Se remueve & y espacios que queden
                line[i-1] = '\0';
                trim(line);
            }
        }

        // Tokenizar (espacios/tabs) para luego pasar a execvp
        int argc = 0;
        char *tok = strtok(line, " \t");
        while (tok != NULL && argc < (MAX_ARGS - 1))
        {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t");
        }
        argv[argc] = NULL;  // En execvp la lista de argumentos debe terminar en NULL

        if (argc == 0) continue;

        // Builtins (exit y cd)
        if (strcmp(argv[0], "exit") == 0)
        {
            //Esperar por todos los proces pendientes
            pid_t pid;
            int status;
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            {
                printf("Reaped child %d (status %d)\n", (int)pid, status);
            }
            break;
        }
        if (strcmp(argv[0], "cd") == 0)
        {
            if (argc >= 2)
            {
                if (chdir(argv[1]) == -1)
                {
                    fprintf(stderr, "cd: no se pudo cambiar a la '%s' (iremos a HOME)\n", argv[1]);
                    if (home)
                    {
                        if (chdir(home) == -1) perr("chdir HOME");
                    }
                    else
                    {
                        fprintf(stderr, "cd: HOME no está seteada\n");
                    }
                }
            }
            else // cd sin nada va a home por defecto
            {
                if (home)
                {
                    if (chdir(home) == -1) perr("chdir HOME");
                }
                else
                {
                    fprintf(stderr, "cd: HOME no está seteada\n");
                }
            }
            continue;
        }

        // Ahora se ejecutan comandos

        //datos para las estadísticas de los procesos foreground
        struct timeval tstart, tend;
        struct rusage ru_before, ru_after;
        if (!background)
        {
            if (gettimeofday(&tstart, NULL) == -1) perr("gettimeofday");
            if (getrusage(RUSAGE_CHILDREN, &ru_before) == -1) perr("getrusage");
        }

        // Bloquear SIGCHLD PARA EVITAR RACE con el handler
        if (sigprocmask(SIG_BLOCK, &block_mask, &prev_mask) == -1)
        {
            perr("sigprocmask BLOCK");
        }

        pid_t pid = fork();
        if (pid<0)
        {
            perr("fork");
            if (sigprocmask(SIG_SETMASK, &prev_mask, NULL) == -1) perr("sigprocmask RESTORE");
            continue;
        }
        else if (pid==0) // Hijo es que ejecuta el comando
        {
            // Restaurar la máscara de señales en el hijo: así el hijo recibe SIGCHLD/SIGINT normalmente 
            if (sigprocmask(SIG_SETMASK, &prev_mask, NULL) == -1)
            {
                perr("child sigprocmask RESTORE");
                _exit(EXIT_FAILURE);
            }

            // Por seguridad, se hace un nuevo grupo de procesos de manera que los procesos background no capturen señales dirigidas a los procesos foreground
            if (background) setpgid(0,0);

            if (signal(SIGINT, SIG_DFL) == SIG_ERR){} //Se restaura el poder usar ctrl-c para el proceso hijo (se había deshabilitado para el shell, siendo que solo se puede salir con exit)

            execvp(argv[0], argv);
            perr("falló exec"); // Si execvp retorna, es porque hubo error
            _exit(EXIT_FAILURE);
        }
        else // Proceso padre, cambia un poco con soporte a los procesos en background
        {
            // Restaurar máscara en el padre: ahora el handler puede ejecutarse si hubo SIGCHLD 
            if (sigprocmask(SIG_SETMASK, &prev_mask, NULL) == -1) {
                perr("sigprocmask RESTORE");
            }

            if (background) //Si es background no se bloquea (no espera a que termine)
            {
                printf("Se inicio un proceso background con PID %d\n", (int)pid);
            }
            else // Si es foreground, el shell se queda esperando a que termine
            {
                int status;
                pid_t w;
                do
                {
                    w = waitpid(pid, &status, 0);
                } while (w==-1 && errno==EINTR);
                
                if (w==-1)
                {
                    perr("waitpid");
                }
                else
                {
                    if (WIFEXITED(status)) // Si terminó normalmente
                    {
                        printf("El proceso foreground de PID %d termino con exit code %d\n", (int)pid, WEXITSTATUS(status));
                    }
                    else if (WIFSIGNALED(status)) // Si terminó por una señal
                    {
                        printf("El proceso foreground de PID %d termino con signal %d\n", (int)pid, WTERMSIG(status));
                    }
                    else
                    {
                        printf("El proceso foreground de PID %d termino (status %d)\n", (int)pid, status);
                    }

                    if (gettimeofday(&tend, NULL) == -1) perr("gettimeofday");
                    if (getrusage(RUSAGE_CHILDREN, &ru_after) == -1) perr("getrusage");

                    double wall = (tend.tv_sec - tstart.tv_sec) + (tend.tv_usec - tstart.tv_usec)/1e6;
                    double u_before = ru_before.ru_utime.tv_sec + ru_before.ru_utime.tv_usec/1e6;
                    double s_before = ru_before.ru_stime.tv_sec + ru_before.ru_stime.tv_usec/1e6;
                    double u_after  = ru_after.ru_utime.tv_sec + ru_after.ru_utime.tv_usec/1e6;
                    double s_after  = ru_after.ru_stime.tv_sec + ru_after.ru_stime.tv_usec/1e6;
                    double u = u_after - u_before;
                    double s = s_after - s_before;

                    printf("Tiempos: wall=%.6f s, user=%.6f s, sys=%.6f s\n", wall, u, s);
                }
            }
        }
    }

    return 0;
}
