#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <queue>

#define MAXLEN 1050000
#define MAXUSER 32
using std::queue;
using std::string;
queue<string> send_queue[MAXUSER];

struct ID
{
    pthread_t tid;
    int uid;
    int *puid;
    int *fd;
};

pthread_mutex_t send_mutex[MAXUSER];
pthread_cond_t cv[MAXUSER];
pthread_t recv_thread[MAXUSER];
pthread_t send_thread[MAXUSER];
pthread_t log_thread[MAXUSER];

struct Pipe
{
    int uid;
    int *fd;
};

void *handle_send(void *data)
{
    struct Pipe *pipe = (struct Pipe *)data;
    char buffer[MAXLEN];
    ssize_t len = 0;
    string s;
    while (1)
    {
        if (!send_queue[pipe->uid].empty())
        {
            s = send_queue[pipe->uid].front();
            send_queue[pipe->uid].pop();
            s.copy(buffer, s.size() + 1);
            buffer[s.size()] = '\0';

            // pthread_mutex_unlock(&send_mutex[pipe->uid]);
            while (1)
            {
                len = send(pipe->fd[pipe->uid], buffer, strlen(buffer), 0);
                if (len >= 0 && len < strlen(buffer))
                    strcpy(buffer, buffer + len);
                else
                    break;
            }
        }
    }
    return NULL;
}

void *handle_recv(void *data)
{
    struct Pipe *pipe = (struct Pipe *)data;
    char c;
    string buf, s;
    char msg[20];
    char buffer[MAXLEN];
    sprintf(msg, "Message:");
    s = msg;
    pthread_create(&send_thread[pipe->uid], NULL, handle_send, (void *)pipe);
    // get characters into the recv queue
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(pipe->fd[pipe->uid], buffer, MAXLEN, 0);
        if (len <= 0)
        {
            return 0;
        }

        // buf += c;
        //  when '\n' appears, lock all the mutexes except the mutex of this uid.
        //  then send the signal to wake up the send thread.
        for (int j = 0; j < len; j++)
        {
            buf += buffer[j];
            if (buffer[j] == '\n')
            {

                for (int i = 0; i < MAXUSER; i++)
                {
                    if (!pipe->fd[i] || i == pipe->uid)
                        continue;
                    pthread_mutex_lock(&send_mutex[i]);
                    s = msg + buf;
                    // s += '\n';
                    send_queue[i].push(s);
                    pthread_cond_signal(&cv[i]);
                    pthread_mutex_unlock(&send_mutex[i]);
                }
                buf.clear();
            }
        }
    }
    pthread_cancel(send_thread[pipe->uid]);
    return NULL;
}

int main(int argc, char **argv)
{
    int socketfd, fd[MAXUSER] = {0};
    int port = atoi(argv[1]);
    if ((socketfd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket");
        return 1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    socklen_t addr_len = sizeof(addr);
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)))
    {
        perror("bind");
        return 1;
    }
    if (listen(socketfd, MAXUSER))
    {
        perror("listen");
        return 1;
    }

    struct Pipe pipe[MAXUSER];
    struct ID id[MAXUSER];
    for (int i = 0; i < MAXUSER; i++)
    {
        pipe[i].fd = fd;
        id[i].fd = fd;
    }
    while (1)
    {
        int uid = -1;
        for (int i = 0; i < MAXUSER; i++)
        {
            if (!fd[i])
            {
                uid = i;
                break;
            }
        }
        if (uid == -1)
        {
            continue;
        }
        int fdt = accept(socketfd, NULL, NULL);
        if (fdt == -1)
        {
            perror("accept");
            return 1;
        }
        // printf("user%d connected!\n", uid);

        // clear send queue
        while (!send_queue[uid].empty())
            send_queue[uid].pop();

        // init mutex and cond
        send_mutex[uid] = PTHREAD_MUTEX_INITIALIZER;
        cv[uid] = PTHREAD_COND_INITIALIZER;

        // create recv thread
        pipe[uid].uid = uid;
        fd[uid] = fdt;
        pthread_create(&recv_thread[uid], NULL, handle_recv, (void *)&pipe[uid]);
    }
    return 0;
}