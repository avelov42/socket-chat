#include <boost/version.hpp>
#include <iostream>
#include <string>

using namespace std;

/*
 * Wywołanie programu:

./player host path r-port file m-port md

Parametry:

host   – nazwa serwera udostępniającego strumień audio;
path   – nazwa zasobu, zwykle sam ukośnik;
r-port – numer portu serwera udostępniającego strumień audio,
         liczba dziesiętna;
file   – nazwa pliku, do którego zapisuje się dane audio,
         a znak minus, jeśli strumień audio ma być wysyłany na standardowe
         wyjście (w celu odtwarzania na bieżąco);
m-port – numer portu UDP, na którym program nasłuchuje poleceń,
         liczba dziesiętna;
md     – no, jeśli program ma nie żądać przysyłania metadanych,
         yes, jeśli program ma żądać przysyłania metadanych.
 */

//sanitized arguments:
string server_name, path, r_port, outfile_name, m_port, md;
bool out_to_file, request_metadata;

void init()
{

}







int main()
{
    std::cout << "Boost version: "
          << BOOST_VERSION / 100000
          << "."
          << BOOST_VERSION / 100 % 1000
          << "."
          << BOOST_VERSION % 100
          << std::endl;
    return 0;
}