/*
En este paso ahora se ejecutan los comandos externos en foreground
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

int main(void)
{
    char line[MAX_LINE];  // Línea de comando
    char *argv[MAX_ARGS];
    char *home = getenv("HOME"); //path al home

    // Para evitar el ctrl-c mate al shell (porque solo se puede salir con exit)
    if (signal(SIGINT, SIG_IGN) == SIG_ERR)
    {
        perr("signal(SIGINT)");
    }

    while (1)
    {
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

        // Ahora se ejecutan comandos (por ahora solo en foreground)
        pid_t pid = fork();
        if (pid<0)
        {
            perr("fork");
            continue;
        }
        else if (pid==0) // Hijo es que ejecuta el comando
        {
            if (signal(SIGINT, SIG_DFL) == SIG_ERR){}

            execvp(argv[0], argv);
            perr("falló exec"); // Si execvp retorna, es porque hubo error
            _exit(EXIT_FAILURE);
        }
        else // El padre espera a que termine el hijo
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
            }
        }
    }

    return 0;
}
