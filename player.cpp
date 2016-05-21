#include <iostream>
#include <boost/regex.hpp>

#include "player.h"

using namespace std;

void parse_input(int, char **);

Arguments args;


int main(int argc, char **argv)
{
    parse_input(argc, argv);
    //connect to server
    //open udp
    //execute

    return 0;
}

void parse_input(int argc, char **argv)
{

    const string port_regex{"([0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5])"};
    const string path_regex{"/([\\w\\d]+\\/{0,1})+"};
    const string server_regex{
            "(((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]))|(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9]))"};
    const string filename_regex{"-|(/)?([^/\\0]+(/)?)+"};
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

        pattern = filename_regex;
        if(boost::regex_match(argv[4], pattern))
            args.outfile_name = argv[4];
        else throw std::invalid_argument("output file");

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
        cerr << "Problem with arguments: " << e.what() << endl;
        exit(INVALID_ARGUMENT_EXIT_CODE);
    }

    /*
    out_to_file = (args.outfile_name != "-");
    request_metadata = (args.md_string == "yes");
     */

    if(DEBUG)
    {
        cout << "All OK" << std::endl;
        cout << args.server_name << endl << args.path_name << endl << args.r_port << endl << args.outfile_name <<
        endl << args.m_port << endl <<
        args.md_string << endl; //<< "out_to_file: " << out_to_file << endl << "request_metadata: " << request_metadata <<
        //endl;
    }
}
