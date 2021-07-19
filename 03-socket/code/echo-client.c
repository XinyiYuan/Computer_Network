/* client application */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>


char* handle_recv_msg(char *msg, char * reply);
 
int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in server;
    char message[1000], server_reply[2000];
     
    // create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("create socket failed\n");
		return -1;
    }
    printf("socket created\n");
     
    server.sin_addr.s_addr = inet_addr("10.0.0.1");
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
 
    // connect to server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect failed\n");
        return 1;
    }
     
    printf("connected\n");
    
    //lab03 added
    
    char request_head[] = 
    " HTTP/1.1\r\n"
    "Accept: */*\r\n"
    "Host: 10.0.0.1\r\n"
    "Connection: Keep-Alive\r\n\r\n";
    

    FILE *file;
    char file_name[10];
    char recv_file_name[15]="recv_";
    char processed_msg[200];
    int len;
    
    while(1) {
        memset(file_name,0,sizeof(file_name));
        memset(recv_file_name,0,sizeof(recv_file_name));
        memset(processed_msg,0,sizeof(processed_msg));
        memset(message,0,sizeof(message));
        memset(server_reply,0,sizeof(server_reply));
        
        strcat(recv_file_name,"recv_");
        printf("--------------------------------------\n");
        printf("Please enter file name: ");
        scanf("%s", file_name);
        strcat(recv_file_name,file_name);
//        printf("recv_path=%s\n",recv_path);
        
        strcpy(message, "GET /");
        strcat(message, file_name);
//        printf("message=%s\n",message);
        strcat(message, request_head);
        printf("Send message:\n%s",message);
        
        
        // send some data
        if (send(sock, message, strlen(message), 0) < 0) {
            printf("send failed\n");
            return 1;
        }
        printf("send succeeded\n");
        
        // receive a reply from the server
        
        len = recv(sock, server_reply, 2000, 0);
        if (len < 0) {
            printf("recv failed\n");
            break;
        }
        else if (len == 0){
            printf("connection closed\n");
            break;
        }
        else printf("recv succeeded\n");
        server_reply[len] = 0;
        
        printf("len:%d\n",len);
        printf("server reply:%s\n",server_reply);
        
        if(strstr(server_reply, "404 Not Found")){
            printf("HTTP 404 Not Found\n");
            continue;
        }
        
        file = fopen(recv_file_name, "wb+"); //create a new file
        
        if (handle_recv_msg(processed_msg,server_reply) != NULL){
            fwrite(processed_msg, 1, strlen(processed_msg), file);
        }
        else 
            printf("save failed\n");
        
        char * result;
        if ((result = strtok(server_reply,"\r\n")) != NULL)
            printf("%s\n", result);
        
        fclose(file);
        
        //test with SimpleHttpServer
        close(sock);
        sock = socket(AF_INET, SOCK_STREAM, 0);
        connect(sock, (struct sockaddr *)&server, sizeof(server));
    }
    
    close(sock);
    return 0;
}

//lab03 added
char* handle_recv_msg(char *msg, char * reply){
    int cursor=0;
    int flag=0;
    
    for( ;cursor<strlen(reply)-3;cursor++){
        if (reply[cursor]=='\r' && reply[cursor+1]=='\n' && reply[cursor+2]=='\r' && reply[cursor+3]=='\n') {
            flag = 1;
            break;
        }
    }
    
    if (flag==0) 
        return NULL;
    
    cursor=cursor+4;
    if(cursor<strlen(reply)) {
        strcpy(msg, &reply[cursor]);
        return &reply[cursor];
    }
    else
        return NULL;

}
