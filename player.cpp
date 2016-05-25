#include <iostream>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <boost/regex.hpp>
#include <sys/poll.h>
#include <fcntl.h>
#include <bits/stl_queue.h>

#include "player.h"
#include "common.h"

#define log printf

using namespace std;

const int RADIO = 0;
const int MASTER = 1;
const int HEADER_READ_SIZE = 128;
const int HEADER_MAX_SIZE = 4096;
const int RADIO_BUFFER_SIZE = 777;
const int MASTER_BUFFER_SIZE = 128;
const int METADATA_MAX_SIZE = 4080;
const int METADATA_SIZE_FACTOR = 16; //bytes per unit in length field in metadata

void init(int, char **);
void connect_to_radio();
void get_header();
void get_metaint();
void main_loop();
void die(int, const char *);
void get_metaint();
void log_radio_buffer();
void main_loop();
void handle_master_command();
void handle_radio_stream();

void fix_header_overflow();
void open_master_socket();
void show_metadata();

Arguments args;
struct pollfd socket_to[2];
int out_fd;
int icy_metaint;
bool quit;
bool paused;

string header_buffer; //storage for header, string to find

char rbuffer[RADIO_BUFFER_SIZE];
int rbuffer_pos; //position to write (from socket)

queue<char> audio;
queue<char> metadata_tmp;
queue<char> metadata_rdonly;

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


//while looking here keep in mind - computers are fast, programmers are lazy.
void handle_radio_stream()
{

    static int bytes_from_last_md = 0;
    static int md_read = 0;
    static int md_length = 0;
    static bool reading_md = false;


    //at this moment, there maybe some data or metedata (from header overflow) in rbuffer

    //clears rbuffer
    for(int i = 0; i < rbuffer_pos; i++)
    {
        //log("%d %d %d\n", bytes_from_last_md, md_read, md_length);


        if(bytes_from_last_md == icy_metaint) //md length byte
        {
            md_length = ((unsigned char) rbuffer[i]) * METADATA_SIZE_FACTOR;
            bytes_from_last_md = 0;
            reading_md = true;
            printf("%d\n", md_length);
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
                    debug_print_md();
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
    negative_is_bad(rbuffer_pos = read(socket_to[RADIO].fd, rbuffer, RADIO_BUFFER_SIZE),
                    "read from socket (radio)");

    if(rbuffer_pos == 0)
        die(0, "server closed connection");
}


void main_loop()
{

    while(!quit)
    {
        socket_to[RADIO].revents = 0;
        socket_to[MASTER].revents = 0;

        negative_is_bad(poll(socket_to, 2, -1), "poll error!");

        if(socket_to[RADIO].revents == POLLHUP)
            die(0, "server disconnected");
        if(socket_to[RADIO].revents == POLLERR || socket_to[MASTER].revents == POLLERR)
            die(0, "kitty has eaten ethernet cable, not my fault");
        if(socket_to[RADIO].revents == POLLIN)
            handle_radio_stream();
        if(socket_to[MASTER].revents == POLLIN)
            handle_master_command();
    }
    die(0, "quit on demand");
}

/** ************************************************** */

void handle_master_command()
{
    ssize_t received;
    char msg_buffer[MASTER_BUFFER_SIZE];
    struct sockaddr_in address;
    socklen_t addr_len = sizeof(address);

    negative_is_bad(received = recvfrom(socket_to[MASTER].fd, msg_buffer, MASTER_BUFFER_SIZE, 0,
                                        (struct sockaddr *) &address, &addr_len), "recvfrom error");
    msg_buffer[received] = '\0';
    string command(msg_buffer);


    //@todo remove \n
    if(command == "PLAY\n")
        paused = false;
    if(command == "PAUSE\n")
        paused = true;
    if(command == "QUIT\n")
        quit = true;

    if(command == "TITLE\n")
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
            negative_is_bad(sendto(socket_to[MASTER].fd, what[1].str().c_str(), what[1].str().size(), 0, (struct sockaddr*) &address,
                                    addr_len), "sendto error");
        }




    }

    cout << command << endl;
}


//copies first data from header buffer to radio buffer
void fix_header_overflow()
{
    size_t end = header_buffer.find("\r\n\r\n");
    header_buffer.erase(header_buffer.begin(), header_buffer.begin() + end + 4);
    memmove(rbuffer, header_buffer.c_str(), header_buffer.size());
    rbuffer_pos = header_buffer.size();
}

void get_metaint()
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

void log_radio_buffer()
{
    log("\n~~~ ~~~ ~~~ ~~~\n");
    log("Buffer size: %d\n", (int) rbuffer_pos);
    for(int i = 0; i < rbuffer_pos; i++)
        log("%c", rbuffer[i]);
    log("\n~~~ ~~~ ~~~ ~~~\n");
}

void get_header()
{
    char header_out[64];
    //operator== conversion from char* literal to string? hope so! @bug
    sprintf(header_out, "GET %s HTTP/1.0\r\nIcy-MetaData:%d\r\n\r\n", args.path_name.c_str(),
            args.md_string == "yes" ? 1 : 0);
    safe_all_write(socket_to[RADIO].fd, header_out, strlen(header_out));
    log("Header sent!\n");

    int rv = poll(socket_to, 2, 5000); //waiting 5 sec for data
    if(rv == 0) die(1, "awaiting for data timeout");
    else if(rv < 0) die(1, "poll error");

    ssize_t readed;
    char header_in[HEADER_READ_SIZE];
    do
    {
        readed = read(socket_to[RADIO].fd, header_in, HEADER_READ_SIZE);
        header_buffer.append(header_in, readed); //append works fine with \0 (adds them to string)
        if(header_buffer.size() > HEADER_MAX_SIZE)
            die(1, "too large header");
    }
    while(header_buffer.find("\r\n\r\n") == string::npos);

    log("Header received!\n");

}

void open_master_socket()
{
    struct sockaddr_in server_address;
    negative_is_bad(socket_to[MASTER].fd = socket(AF_INET, SOCK_DGRAM, 0), "cannot get a socket (master)");

    unsigned short port_num = stoi(args.m_port);
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port_num);

    negative_is_bad(bind(socket_to[MASTER].fd, (struct sockaddr *) &server_address,
                         (socklen_t) sizeof(server_address)), "cannot bind socket (master)");
}

void connect_to_radio()
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

void init(int argc, char **argv)
{

    const string port_regex{"([0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5])"};
    const string path_regex{"\\/([\\w\\d]+\\/{0,1})*"};
    const string server_regex{
            "(((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]))|(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9]))"};
    const string filename_regex{"-|(\\/)?([^/\\0]+(/)?)+"};
    const string metadata_regex{"yes|no"};
    boost::regex pattern;
    try
    {
        if(argc != 7)
            throw invalid_argument("number of arguments");

        pattern = server_regex;
        if(boost::regex_match(argv[1], pattern))
            args.server_name = argv[1];
        else throw invalid_argument("server name");

        pattern = path_regex;
        if(boost::regex_match(argv[2], pattern))
            args.path_name = argv[2];
        else throw invalid_argument("path name");

        pattern = port_regex;
        if(boost::regex_match(argv[3], pattern))
            args.r_port = argv[3];
        else throw std::invalid_argument("r_port");

        string filename(argv[4]);
        if(filename == "-")
            out_fd = STDOUT_FILENO;
        else
            negative_is_bad(out_fd = open(argv[4], O_CREAT | O_WRONLY, 0666), "problem with argument: path_name");

        pattern = port_regex;
        if(boost::regex_match(argv[5], pattern))
            args.m_port = argv[5];
        else throw std::invalid_argument("m_port");

        pattern = metadata_regex;
        if(boost::regex_match(argv[6], pattern))
            args.md_string = argv[6];
        else throw std::invalid_argument("metadata yes/no");
    }
    catch(boost::regex_error &e)
    {
        cerr << pattern << " is not a valid regular expression: \"" << e.what() << "\"" << endl;
    }
    catch(std::invalid_argument &e)
    {
        string error("problem with argument: ");
        error.append(e.what());
        die(1, error.c_str());
    }

    socket_to[RADIO].events = POLLIN | POLLHUP | POLLERR;
    socket_to[MASTER].events = POLLIN | POLLERR;
    socket_to[RADIO].revents = socket_to[MASTER].revents = 0;
    icy_metaint = 0;
    quit = false;

    log("Arguments: OK\nserver_name: %s\npath_name: %s\nradio_port: %s\noutfile_name: %s\nmaster_port: %s\nmd_string: %s\n",
        args.server_name.c_str(), args.path_name.c_str(), args.r_port.c_str(), args.outfile_name.c_str(),
        args.m_port.c_str(), args.md_string.c_str());

}


void die(int code, const char *reason)
{
    //static bool been_here = false; //die cannot be called from inside because it may cause infinite loop
    //if(been_here) fatal("OS failure");
    //been_here = true;
    if(code != 0) //client cannot output nothing else than messages
        printf("Player is turning off: %s\n", reason);

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
    static_assert(RADIO_BUFFER_SIZE > HEADER_READ_SIZE, "fix it");
    init(argc, argv);
    connect_to_radio();
    open_master_socket();
    get_header();
    if(header_buffer.find("ICY 200 OK") == string::npos)
        die(1, "http request rejected");
    else
    {
        if(args.md_string == "yes") get_metaint();
        fix_header_overflow();
        main_loop();

    }

    return 0;
}