/*
 * udpserver.c - A simple UDP echo server
 * usage: udpserver <port>
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>

#define BUFSIZE 1024

struct data_packet
{
    long int seqID;
    long int length;
    char data[BUFSIZE];
};

/*
 * error - wrapper for perror
 */
void error(char *msg)
{
    perror(msg);
    exit(1);
}

// ls helper function
int ls(FILE *f)
{
    struct dirent **dirent;
    int n = 0;

    if ((n = scandir(".", &dirent, NULL, alphasort)) < 0)
    {
        perror("Scanerror");
        return -1;
    }

    while (n--)
    {
        fprintf(f, "%s\n", dirent[n]->d_name);
        free(dirent[n]);
    }

    free(dirent);
    return 0;
}

int main(int argc, char **argv)
{
    int sockfd;                    /* socket */
    int portno;                    /* port to listen on */
    int clientlen;                 /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct hostent *hostp;         /* client host info */
    char option[10];
    char file[100];
    char input[BUFSIZE];
    char *hostaddrp; /* dotted decimal host addr string */
    int optval;      /* flag value for setsockopt */

    struct stat st;
    struct data_packet frame;
    struct timeval t_out = {0, 0};

    ssize_t numRead;
    ssize_t length;
    off_t f_size;
    long int ackNum = 0; // Recieve frame acknowledgement
    int ackSend = 0;

    FILE *fptr;

    /*
     * check command line arguments
     */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    /*
     * socket: create the parent socket
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets
     * us rerun the server immediately after we kill it;
     * otherwise we have to wait about 20 secs.
     * Eliminates "ERROR on binding: Address already in use" error.
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /*
     * bind: associate the parent socket with a port
     */
    if (bind(sockfd, (struct sockaddr *)&serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    /*
     * main loop: wait for a datagram, then echo it
     */
    clientlen = sizeof(clientaddr);

    while (1)
    {

        /*
         * recvfrom: receive a UDP datagram from a client
         */
        bzero(option, 10);
        bzero(file, 100);
        memset(input, 0, sizeof(BUFSIZE));
        length = sizeof(clientaddr);
        int n = recvfrom(sockfd, input, BUFSIZE - 1, 0,
                         (struct sockaddr *)&clientaddr, &clientlen);
        if (n < 0)
            error("ERROR in recvfrom");
        input[n] = '\0';
        // else send ack
        printf("Server: The recieved message - %s\n", input);

        sscanf(input, "%s %s", option, file);

        /*
         * gethostbyaddr: determine who sent the datagram
         */
        // hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
        // 	  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        // if (hostp == NULL)
        //   error("ERROR on gethostbyaddr");
        // hostaddrp = inet_ntoa(clientaddr.sin_addr);
        // if (hostaddrp == NULL)
        //   error("ERROR on inet_ntoa\n");
        // printf("server received datagram from %s (%s)\n",
        //  hostp->h_name, hostaddrp);
        // printf("server received %d/%d bytes: %s\n", strlen(buf), n, buf);
        hostaddrp = inet_ntoa(clientaddr.sin_addr);
        if (hostaddrp == NULL)
            error("ERROR on inet_ntoa\n");
        printf("server received datagram from %s\n", hostaddrp);

        /*
         * sendto: echo the input back to the client
         */
        if (option[0] == '1')
        {
            printf("Server: Get called with file name: %s\n", file);
            if (access(file, F_OK) == 0)
            { // Check if file exist

                int total_frame = 0, resend_frame = 0, drop_frame = 0, failure = 0;
                long int i = 0;

                stat(file, &st); // creating struct for file size
                f_size = st.st_size; // Size of the file

                t_out.tv_sec = 1;
                t_out.tv_usec = 0;
                // Set timeout option for recvfrom
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)); 

                fptr = fopen(file, "rb"); // open the file to be sent

                // Total number of frames to be sent
                if ((f_size % BUFSIZE) != 0)
                    total_frame = (f_size / BUFSIZE) + 1; 
                else
                    total_frame = (f_size / BUFSIZE);

                printf("Total number of packets: %d\n", total_frame);

                length = sizeof(clientaddr);

                // Send number of packets (to be transmitted) to reciever and get the ack
                sendto(sockfd, &(total_frame), sizeof(total_frame), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr)); 
                recvfrom(sockfd, &(ackNum), sizeof(ackNum), 0, (struct sockaddr *)&clientaddr, (socklen_t *)&length);

                while (ackNum != total_frame) // Check for the acknowledgement
                {
                    // keep Retrying until the ack matches
                    sendto(sockfd, &(total_frame), sizeof(total_frame), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
                    recvfrom(sockfd, &(ackNum), sizeof(ackNum), 0, (struct sockaddr *)&clientaddr, (socklen_t *)&length);

                    resend_frame++;

                    // Enable timeout flag even if it fails after 20 tries
                    if (resend_frame == 20)
                    {
                        failure = 1;
                        break;
                    }
                }

                // transmit data frames sequentially followed by an acknowledgement matching
                for (i = 1; i <= total_frame; i++)
                {
                    //reset the frame at the beginning of each iteration
                    memset(&frame, 0, sizeof(frame));
                    ackNum = 0;
                    frame.seqID = i;
                    frame.length = fread(frame.data, 1, BUFSIZE, fptr);

                    // send the frame and get ack
                    sendto(sockfd, &(frame), sizeof(frame), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));       
                    recvfrom(sockfd, &(ackNum), sizeof(ackNum), 0, (struct sockaddr *)&clientaddr, (socklen_t *)&length); 

                    while (ackNum != frame.seqID) // Check for ack
                    {
                        // keep retrying until the ack matches
                        sendto(sockfd, &(frame), sizeof(frame), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
                        recvfrom(sockfd, &(ackNum), sizeof(ackNum), 0, (struct sockaddr *)&clientaddr, (socklen_t *)&length);
                        printf("frame: %ld	dropped, %d times\n", frame.seqID, ++drop_frame);

                        resend_frame++;

                        printf("frame: %ld	dropped: %d times\n", frame.seqID, drop_frame);

                        // Enable the timeout flag if it fails after 200 tries
                        if (resend_frame == 200)
                        {
                            failure = 1;
                            break;
                        }
                    }

                    resend_frame = 0;
                    drop_frame = 0;

                    // File transfer fails if timeout occurs
                    if (failure == 1)
                    {
                        printf("File not sent\n");
                        break;
                    }

                    printf("frame: %ld   	Ack:  %ld \n", i, ackNum);

                    if (total_frame == ackNum)
                        printf("File sent\n");
                }
                fclose(fptr);

                // Disable the timeout option
                t_out.tv_sec = 0;
                t_out.tv_usec = 0;
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)); 
            }
            else
            {
                printf("Invalid Filename\n");
            }
        }
        else if (option[0] == '2')
        {
            printf("Server: Put called with file name: %s\n", file);

            long int total_frame = 0, bytes_rec = 0, i = 0;

            // Enable the timeout option if client does not respond
            t_out.tv_sec = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval));

            // Get the total number of frames to recieve
            recvfrom(sockfd, &(total_frame), sizeof(total_frame), 0, (struct sockaddr *)&clientaddr, (socklen_t *)&length);

            // Disable the timeout option
            t_out.tv_sec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)); 

            // check to make sure the file has content
            if (total_frame > 0)
            {
                sendto(sockfd, &(total_frame), sizeof(total_frame), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
                printf("Total frame: %ld\n", total_frame);

                fptr = fopen(file, "wb"); // open the file in write mode

                // Recieve all the frames and send the acknowledgement sequentially
                for (i = 1; i <= total_frame; i++)
                {
                    //reset frame data on each iteration
                    memset(&frame, 0, sizeof(frame));

                    // recieve frame and send ack
                    recvfrom(sockfd, &(frame), sizeof(frame), 0, (struct sockaddr *)&clientaddr, (socklen_t *)&length);         
                    sendto(sockfd, &(frame.seqID), sizeof(frame.seqID), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr)); 

                    // Drop the repeated frame
                    if ((frame.seqID < i) || (frame.seqID > i))
                    {
                        i--;
                    }
                    else
                    {
                        // Write the recieved data to the file
                        fwrite(frame.data, 1, frame.length, fptr); 
                        printf("frame.seqID: %ld	frame.length: %ld\n", frame.seqID, frame.length);
                        bytes_rec += frame.length;
                    }

                    if (i == total_frame)
                        printf("File recieved\n");
                }
                printf("Total bytes recieved: %ld\n", bytes_rec);
                fclose(fptr);
            }
            else
            {
                printf("File is empty\n");
            }
            bzero(input, 120);
        }

        // delete option
        else if (option[0] == '3')
        {
            // Check if file exist
            if (access(file, F_OK) == -1)
            { 
                ackSend = -1;
                sendto(sockfd, &(ackSend), sizeof(ackSend), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
            }
            else
            {
                // Check if file has appropriate permission
                if (access(file, R_OK) == -1)
                { 
                    ackSend = 0;
                    sendto(sockfd, &(ackSend), sizeof(ackSend), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
                }
                else
                {
                    printf("Filename is %s\n", file);
                    remove(file); // delete the file
                    ackSend = 1;
                    // send the positive acknowledgement
                    sendto(sockfd, &(ackSend), sizeof(ackSend), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr)); 
                }
            }
        }

        // list directory option
        else
        {
            char file_entry[200]; // create buffer for ls to be stored in
            memset(file_entry, 0, sizeof(file_entry));

            fptr = fopen("a.log", "wb"); // Create a file with write permission

            // get the list of files in present directory
            if (ls(fptr) == -1)
            { 
                error("ls");
            }
            fclose(fptr);

            fptr = fopen("a.log", "rb");
            // read the directory into the buffer
            int filesize = fread(file_entry, 1, 200, fptr);

            printf("Filesize = %d	%ld\n", filesize, strlen(file_entry));

            // Send the file list
            if (sendto(sockfd, file_entry, filesize, 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) == -1)
            { 
                error("Server: send");
            }
            remove("a.log"); // delete the temp file
            fclose(fptr);
        }
    }

    return 1;
}
