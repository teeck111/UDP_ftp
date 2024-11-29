/*
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
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
#include <dirent.h> // for the timeout

#define BUFSIZE 1024
#define TIMEOUT_SECS 2
#define MAX_RETRIES 5

struct data_packet
{
    long int seqID;
    long int length;
    char data[BUFSIZE];
};
/*
 * error - wrapper for perror
 */
error(char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char **argv)
{
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in send_addr, from_addr;
    struct hostent *server;
    char *hostname;
    char option[10];
    char file[100];
    char input[120];
    char ack_send[4] = "ACK";

    struct stat st;
    struct data_packet frame;
    struct timeval t_out = {0, 0}; // inits the time out to 0 seconds and 0 micro seconds

    ssize_t numRead = 0;
    ssize_t length = 0;
    off_t f_size = 0;
    long int ackNum = 0;
    int ackRecieve = 0;

    FILE *fptr;

    // check command line arguments
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <hostname> <port>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    // create the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    // gethostbyname: get the server's DNS entry
    server = gethostbyname(hostname);
    if (server == NULL)
    {
        fprintf(stderr, "ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *)&send_addr, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&send_addr.sin_addr.s_addr, server->h_length);
    send_addr.sin_port = htons(portno);

    /* get the users option */
    bzero(option, 10);
    printf("Would you like to: \n"
           "1. Get a file \n"
           "2. Put a file \n"
           "3. Delete a file \n"
           "4. See your files \n"
           "5. Exit \n"
           "Type just the number of the option you want to pick: ");
    fgets(option, 10, stdin);

    /* get the file from the user */
    printf("User input: %s \n", option);

    while (!(strcmp(option, "1\n") == 0 || strcmp(option, "2\n") == 0 || strcmp(option, "3\n") == 0 || strcmp(option, "4\n") == 0 || strcmp(option, "5\n") == 0))
    {
        printf("Invalid input, please enter a valid option. \n");

        bzero(option, 10);
        printf("Would you like to: \n"
               "1. Get a file \n"
               "2. Put a file \n"
               "3. Delete a file \n"
               "4. See your files \n"
               "5. Exit \n"
               "Type just the number of the option you want to pick: ");
        fgets(option, 10, stdin);
    }

    if (option[0] == '1' || option[0] == '2' || option[0] == '3')
    {
        bzero(file, 100);
        printf("Input the file you want the action performed on: ");
        fgets(file, 100, stdin);
    }
    else
    {
        file[0] = 'E'; // For when the file name doesn't need to be sent
    }
    printf("File inputed: %s \n", file);

    snprintf(input, sizeof(input), "%s %s", option, file);

    // sends what option the user wants along with the file name if necessary
    if (sendto(sockfd, input, strlen(input), 0, (struct sockaddr *)&send_addr, sizeof(send_addr)) == -1)
    {
        error("Client: send");
    }
    bzero(input, 120);

    // getting a file
    if (option[0] == '1')
    {
        printf("Entering the get function \n");
        long int frames = 0;
        long int bytes_rec = 0, i = 0;

        // Enable the timeout option for if client does not respond
        t_out.tv_sec = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval));

        // Get the total number of frame to recieve
        recvfrom(sockfd, &(frames), sizeof(frames), 0, (struct sockaddr *)&from_addr, (socklen_t *)&length);

        // Disable the timeout option
        t_out.tv_sec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)); 

        // check to make sure there is info in the file
        if (frames > 0)
        {
            // send ack
            sendto(sockfd, &(frames), sizeof(frames), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
            printf(": %ld\n", frames);

            fptr = fopen(file, "wb"); // open the file in write mode

            // Recieve all the frames and send the acknowledgement sequentially
            for (i = 1; i <= frames; i++)
            {
                memset(&frame, 0, sizeof(frame));

                // recieve packet
                recvfrom(sockfd, &(frame), sizeof(frame), 0, (struct sockaddr *)&from_addr, (socklen_t *)&length);
                // send ack
                sendto(sockfd, &(frame.seqID), sizeof(frame.seqID), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));

                // Drop the repeated frame to ensure order
                if ((frame.seqID < i) || (frame.seqID > i))
                    i--;
                else
                {
                    fwrite(frame.data, 1, frame.length, fptr); // Write the recieved data to the file
                    printf("frame.seqID : %ld	frame.length : %ld\n", frame.seqID, frame.length);
                    bytes_rec += frame.length; // Incrementing how many totaly bytes have been recieved
                }

                if (i == frames)
                {
                    printf("File recieved\n");
                }
            }
            printf("Total bytes recieved : %ld\n", bytes_rec);
            fclose(fptr);
        }
        else
        {
            printf("File is empty\n");
        }
    }

    // putting a file
    else if (option[0] == '2')
    {
        printf("Putting file %s", file);
        size_t len = strlen(file);

        // To ensure that the file name is properly read
        if (len > 0 && file[len - 1] == '\n')
        {
            file[len - 1] = '\0'; // Replace newline with null terminator
        }

        // Check to see if the file exists and has proper permissions
        if (access(file, F_OK) != 0)
        {
            perror("Error accessing file");
        }
        else if (access(file, F_OK) == 0)
        {
            int frames = 0, resend_frame = 0, drop_frame = 0, failure = 0;
            long int i = 0;

            // initiates the stat struct with the file
            stat(file, &st);
            f_size = st.st_size; // stores the size of the file

            // Set timeout option for recvfrom
            t_out.tv_sec = 1;
            t_out.tv_usec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)); 

            fptr = fopen(file, "rb"); // Open the file to be sent

            if ((f_size % BUFSIZE) != 0)
                // Total number of frames to be sent, accomdates for the frame the wont be full
                frames = (f_size / BUFSIZE) + 1;
            else
                frames = (f_size / BUFSIZE);

            printf("Total number of packets : %d	File size: %lld\n", frames, f_size);

            // Send the number of packets (to be transmitted) to reciever
            sendto(sockfd, &(frames), sizeof(frames), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
            // recieve the ack
            recvfrom(sockfd, &(ackNum), sizeof(ackNum), 0, (struct sockaddr *)&from_addr, (socklen_t *)&length);

            printf("Ack num : %ld\n", ackNum);

            // check to make sure the server knows how many frames it will recieve
            while (ackNum != frames)
            {
                // Keep retrying until ack match
                sendto(sockfd, &(frames), sizeof(frames), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                recvfrom(sockfd, &(ackNum), sizeof(ackNum), 0, (struct sockaddr *)&from_addr, (socklen_t *)&length);

                resend_frame++;

                /*Enable timeout flag after 20 tries*/
                if (resend_frame == 20)
                {
                    failure = 1;
                    break;
                }
            }

            // transmit data frames sequentially followed by an acknowledgement matching
            for (i = 1; i <= frames; i++)
            {
                // reset packets to 0 
                memset(&frame, 0, sizeof(frame));
                ackNum = 0;
                frame.seqID = i;
                frame.length = fread(frame.data, 1, BUFSIZE, fptr);

                // sending the packet
                sendto(sockfd, &(frame), sizeof(frame), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                // recieving the ack
                recvfrom(sockfd, &(ackNum), sizeof(ackNum), 1, (struct sockaddr *)&from_addr, (socklen_t *)&length);

                // Check for the ack match
                while (ackNum != frame.seqID)
                {
                    sendto(sockfd, &(frame), sizeof(frame), 0, (struct sockaddr *)&send_addr, sizeof(send_addr));
                    recvfrom(sockfd, &(ackNum), sizeof(ackNum), 0, (struct sockaddr *)&from_addr, (socklen_t *)&length);
                    printf("frame : %ld	dropped, %d times\n", frame.seqID, ++drop_frame);
                    resend_frame++;

                    // Enable timeout flag after 200 tries
                    if (resend_frame == 200)
                    {
                        failure = 1;
                        break;
                    }
                }
                drop_frame = 0;
                resend_frame = 0;

                // File transfer fails if timeout occurs
                if (failure == 1)
                {
                    printf("File not sent\n");
                    break;
                }

                printf("frame : %ld	Ack : %ld\n", i, ackNum);

                if (frames == ackNum)
                    printf("File sent\n");
            }
            fclose(fptr);

            printf("Disable the timeout\n");
            // Disable timeout
            t_out.tv_sec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval));
        }
        else
        {
            perror("Error accessing file");
        }
    }

    // deleting a file
    else if (option[0] == '3')
    {
        length = sizeof(from_addr);
        ackRecieve = 0;

        // ack from server
        if ((numRead = recvfrom(sockfd, &(ackRecieve), sizeof(ackRecieve), 0, (struct sockaddr *)&from_addr, (socklen_t *)&length)) < 0) 
            error("recieve");

        if (ackRecieve > 0)
            printf("Client: Deleted the file\n");
        else if (ackRecieve < 0)
            printf("Client: Invalid file name\n");
        else
            printf("Client: File does not have appropriate permission\n");
    }

    // ls
    else if (option[0] == '4')
    {
        // creating a new temporary file for the info that is sent back
        char filename[200];
        memset(filename, 0, sizeof(filename));

        length = sizeof(from_addr);

        if ((numRead = recvfrom(sockfd, filename, sizeof(filename), 0, (struct sockaddr *)&from_addr, (socklen_t *)&length)) < 0)
            error("recieve");

        if (filename[0] != '\0')
        {
            printf("Number of bytes recieved = %ld\n", numRead);
            printf("\nThis is the List of files and directories :  \n%s \n", filename);
        }
        else
        {
            printf("Recieved buffer is empty\n");
            // continue;
        }
    }
    else if (option[0] == '5')
    {
        printf("Quitting...\n");
        return 0;
    }
    else
    {
        printf("Incorrect input \n");
        return 0;
    }

    // send the message to the server 
    printf("About to end \n");

    close(sockfd);

    return 0;
}
