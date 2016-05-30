#include <netinet/in.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <netdb.h>

#include <boost/regex.hpp>
#include <queue>
#include "player.h"
#include "common.h"

#define log _ignore

using namespace std;

const int RADIO = 0; //indexes in struct pollfd
const int MASTER = 1;

const int HEADER_READ_SIZE = 128; //portion of single read while receiving header
const int HEADER_MAX_SIZE = 4096; //if header is longer, player finishes
const int RADIO_BUFFER_SIZE = 777; //buffer for audio/md data from radio server
const int MASTER_BUFFER_SIZE = 128; //buffer for master messages
const int METADATA_SIZE_FACTOR = 16; //bytes per unit in length field in metadata

void init(int, char **); //parse arguments, set globals
void setup_radio_connection(); //create tcp connection to radio server
void setup_master_socket(); //create & bind udp socket

void main_loop(); //poll loop
void handle_header();
void handle_master_command();
void handle_radio_stream();

void die(int, const char *);

void debug_print_md();
void debug_print_rbuffer();

Arguments args; //arguments sanitized & converted to string
struct pollfd socket_to[2];
struct timespec last_contact;
int out_fd; //output file descriptor
int icy_metaint;
bool quit;
bool paused;
bool handshake_done;

string header_buffer; //storage for header, string to .find()

char rbuffer[RADIO_BUFFER_SIZE];
int rbuffer_pos; //position to write (from socket)

queue<char> audio;
queue<char> metadata_tmp;
queue<char> metadata_rdonly;

void init(int argc, char **argv)
{
    // ./player host path r-port file m-port md
    // argv[0]  [1]  [2]   [3]   [4]   [5]  [6]
    const string port_regex{"([0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5])"};
    const string metadata_regex{"yes|no"};
    boost::regex pattern;

    {
        if(argc != 7) die(1, "number of arguments");


        args.server_name = argv[1];  //host is verified by getaddrinfo
        args.path_name = argv[2]; //path is verified by server
        args.outfile = argv[4]; //file is verified by open below
        //rest - regex

        if(strcmp(argv[4], "-") == 0) out_fd = STDOUT_FILENO;
        else negative_is_bad(out_fd = open(args.outfile.c_str(), O_CREAT | O_WRONLY, 0666), "invalid output file name");

        pattern = port_regex;
        if(boost::regex_match(argv[3], pattern)) args.r_port = argv[3];
        else die(1, "invalid r-port argument (is it really number?)");

        if(boost::regex_match(argv[5], pattern)) args.m_port = argv[5];
        else die(1, "ivalid m-port argument (is it really number?)");

        pattern = metadata_regex;
        if(boost::regex_match(argv[6], pattern)) args.md_string = argv[6];
        else die(1, "invalid metadata argument (should be yes/no)");
    }


    socket_to[RADIO].events = POLLIN | POLLHUP | POLLERR;
    socket_to[MASTER].events = POLLIN | POLLERR;
    socket_to[RADIO].revents = socket_to[MASTER].revents = 0;
    icy_metaint = 0;
    handshake_done = false;
    quit = false;

    log("Arguments: OK\nserver_name: %s\npath_name: %s\nradio_port: %s\noutfile_name: %s\nmaster_port: %s\nmd_string: %s\n",
        args.server_name.c_str(), args.path_name.c_str(), args.r_port.c_str(), args.outfile.c_str(),
        args.m_port.c_str(), args.md_string.c_str());
}

void setup_radio_connection()
{
    //set up tcp connection
    {
        int rv;
        struct addrinfo addr_hints, *addr_result;

        negative_is_bad(socket_to[RADIO].fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP), "cannot get a socket (radio)");

        memset(&addr_hints, 0, sizeof(struct addrinfo));
        addr_hints.ai_flags = 0;
        addr_hints.ai_family = AF_INET;
        addr_hints.ai_socktype = SOCK_STREAM;
        addr_hints.ai_protocol = IPPROTO_TCP;

        zero_is_ok(rv = getaddrinfo(args.server_name.c_str(), args.r_port.c_str(), &addr_hints, &addr_result),
                   gai_strerror(rv));

        negative_is_bad(connect(socket_to[RADIO].fd, addr_result->ai_addr, addr_result->ai_addrlen),
                        "cannot connect to the server");
        freeaddrinfo(addr_result);

        log("Connection with radio server estabilished!\n");
    }

    //send icy header
    {
        char header_out[64];
        sprintf(header_out, "GET %s HTTP/1.0\r\nIcy-MetaData:%d\r\n\r\n", args.path_name.c_str(),
                args.md_string == "yes" ? 1 : 0);
        safe_all_write(socket_to[RADIO].fd, header_out, strlen(header_out));
        log("Header sent!\n");
    }
}

void setup_master_socket()
{
    struct sockaddr_in server_address;
    negative_is_bad(socket_to[MASTER].fd = socket(AF_INET, SOCK_DGRAM, 0), "cannot get a socket (master)");

    unsigned short port_num = (unsigned short) stoi(args.m_port);
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port_num);

    negative_is_bad(bind(socket_to[MASTER].fd, (struct sockaddr *) &server_address,
                         (socklen_t) sizeof(server_address)), "cannot bind socket (master)");
}

/* *** *** *** LOGIC *** *** *** */

void update_last_contact_time()
{
    clock_gettime(CLOCK_REALTIME, &last_contact);
}

//returns how many ms program should wait for server response
int get_avaiable_time()
{
    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);
    long long curr = current_time.tv_sec * 1000 + current_time.tv_nsec / 1000000;
    long long prev = last_contact.tv_sec * 1000 + last_contact.tv_nsec / 1000000;
    int avail = 5000 - (int) (curr-prev);
    return avail;
}

void main_loop()
{
    int poll_rv;
    update_last_contact_time();
    while(!quit)
    {
        socket_to[RADIO].revents = 0;
        socket_to[MASTER].revents = 0;

        fprintf(stderr, "%d\n", get_avaiable_time());
        negative_is_bad(poll_rv = poll(socket_to, 2, get_avaiable_time()), "poll error!");

        if(poll_rv == 0)
            die(0, "server have not responded since 5 sec");

        if(socket_to[RADIO].revents == POLLHUP)
            die(0, "server disconnected");
        if(socket_to[RADIO].revents == POLLERR || socket_to[MASTER].revents == POLLERR)
            die(0, "kitty has eaten ethernet cable, not my fault");

        if(socket_to[RADIO].revents == POLLIN && handshake_done)
            handle_radio_stream();
        else if(socket_to[RADIO].revents == POLLIN)
            handle_header();
        if(socket_to[MASTER].revents == POLLIN)
            handle_master_command();
    }
    die(0, "quit on demand");
}

void handle_master_command()
{
    ssize_t received;
    struct sockaddr_in address;
    socklen_t addr_len = sizeof(address);
    char msg_buffer[MASTER_BUFFER_SIZE+1];

    negative_is_bad(received = recvfrom(socket_to[MASTER].fd, msg_buffer, MASTER_BUFFER_SIZE, 0,
                                        (struct sockaddr *) &address, &addr_len), "recvfrom error");
    msg_buffer[received] = '\0';
    string command(msg_buffer);

    if(command == "PLAY")
        paused = false;
    else if(command == "PAUSE")
        paused = true;
    else if(command == "QUIT")
        quit = true;
    else if(command == "TITLE")
    {
        string metadata;
        queue<char> tmp = metadata_rdonly;
        for(int i = 0; i < metadata_rdonly.size(); i++)
        {
            metadata.push_back(tmp.front());
            tmp.pop();
        }
        boost::regex stream_title{"StreamTitle='(.*?)';"};
        boost::smatch what;
        boost::regex_search(metadata, what, stream_title);

        if(what.size() == 2)
        {
            negative_is_bad(sendto(socket_to[MASTER].fd, what[1].str().c_str(), what[1].str().size(), 0,
                                   (struct sockaddr *) &address,
                                   addr_len), "sendto error");
        }
    }
    else fprintf(stderr, "Unrecognized command %s, ignored.\n", command.c_str());
}


//while looking here keep in mind - computers are fast, programmers are lazy.
void handle_radio_stream()
{
    static int bytes_from_last_md = 0;
    static int md_read = 0;
    static int md_length = 0;
    static bool reading_md = false;
    //within first entry to this function,
    //there maybe some data or metedata (from header overflow) in rbuffer
    update_last_contact_time();
    //clears rbuffer
    for(int i = 0; i < rbuffer_pos; i++)
    {
        if(bytes_from_last_md == icy_metaint) //md length byte
        {
            md_length = ((unsigned char) rbuffer[i]) * METADATA_SIZE_FACTOR;
            bytes_from_last_md = 0;
            reading_md = true;
            continue;
        }
        if(!reading_md) //normal audio data
        {
            if(!paused) audio.push(rbuffer[i]);
            bytes_from_last_md++;
            continue; //jump to next byte
        }
        if(reading_md) //metadata byte
        {
            if(md_read == md_length) //all metadata has been read
            {
                //store current md in rdonly queue
                if(md_length != 0)
                {
                    metadata_rdonly = metadata_tmp;
                    queue<char> empty; //clear tmp metadata
                    swap(metadata_tmp, empty);
                }
                md_read = 0;
                md_length = 0;
                reading_md = false;
                i--; //we didnt processed this byte!
            }
            else
            {
                metadata_tmp.push(rbuffer[i]);
                md_read++;
            }
            continue;
        }
    }
    rbuffer_pos = 0; //all bytes in buffer handled

    //send audio to out
    while(audio.size() > 0)
    {
        char c = audio.front();
        negative_is_bad(write(out_fd, &c, 1), "write error");
        audio.pop();
    }

    //get new data from stream
    //rbuffer must be empty now! (and it is, just reminding)
    negative_is_bad(rbuffer_pos = (int) read(socket_to[RADIO].fd, rbuffer, RADIO_BUFFER_SIZE),
                    "read from socket (radio)");
    if(rbuffer_pos == 0)
        die(0, "server closed connection");
}

void handle_header()
{
    ssize_t readed;
    char header_in[HEADER_READ_SIZE];
    negative_is_bad(readed = read(socket_to[RADIO].fd, header_in, HEADER_READ_SIZE), "read error");
    if(readed == 0)
        die(0, "server closed connection");
    header_buffer.append(header_in, readed); //append works fine with \0 (adds them to string)
    if(header_buffer.size() > HEADER_MAX_SIZE)
        die(1, "too large header");

    if(header_buffer.find("\r\n\r\n") != string::npos) //end of header found
    {
        if(header_buffer.find("ICY 200 OK") == string::npos)
            die(1, "http request rejected");

        //set up icy_metaint
        if(args.md_string == "yes")
        {
            boost::regex pattern{"icy-metaint:(\\d{1, 9})"};
            boost::smatch what;
            boost::regex_search(header_buffer, what, pattern);
            if(what.size() != 2)
                die(1, "no icy-metaint found in header");
            else
                icy_metaint = stoi(what[1]);
            log("icy_metaint is now %d\n", icy_metaint);
        }

        //move overflow (audio data) from header to audio buffer (rbuffer)
        {
            size_t end = header_buffer.find("\r\n\r\n");
            header_buffer.erase(header_buffer.begin(), header_buffer.begin() + end + 4);
            memmove(rbuffer, header_buffer.c_str(), header_buffer.size());
            rbuffer_pos = (int) header_buffer.size();
        }
        handshake_done = true;
    }

    log("Header received!\n");
}

void die(int code, const char *reason)
{
    //if(code != 0) //client cannot output nothing else than messages
        fprintf(stderr, "Player is turning off: %s\n", reason);

    if(socket_to[RADIO].fd > 0)
        close(socket_to[RADIO].fd);
    if(socket_to[MASTER].fd > 0)
        close(socket_to[MASTER].fd);

    if(out_fd != STDOUT_FILENO)
        close(out_fd);

    exit(code);


}

int main(int argc, char **argv)
{
    init(argc, argv);
    setup_radio_connection(); //connect and send header
    setup_master_socket(); //listen on udp
    main_loop(); //
    return 0;
}

/* *** *** *** DEBUG *** *** *** */


void debug_print_md()
{
    char *metadata = new char[metadata_rdonly.size()];
    queue<char> tmp = metadata_rdonly;
    for(int i = 0; i < metadata_rdonly.size(); i++)
    {
        metadata[i] = tmp.front();
        tmp.pop();
    }
    printf("%s\n", metadata);
    for(int i = 0; i < metadata_rdonly.size(); i++)
        printf("%d ", metadata[i]);
    printf("\n");
    delete[] metadata;
}

void debug_print_rbuffer()
{
    log("\n~~~ ~~~ ~~~ ~~~\n");
    log("Buffer size: %d\n", (int) rbuffer_pos);
    for(int i = 0; i < rbuffer_pos; i++)
        log("%c", rbuffer[i]);
    log("\n~~~ ~~~ ~~~ ~~~\n");
}