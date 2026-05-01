#include <cassert>
#include <fstream>
#include <unordered_map>
#include <iostream>
using namespace std;


int main(int argc, const char* argv[])
{
    if (argc != 5 || argv[3] != string("-o")) {
        cerr << "Usage: " << argv[0] << " <mode> <input> -o <output>" << endl;
        return 1;
    }
    string mode = argv[1];
    string input = argv[2];
    string output = argv[4];
    cerr << "Mode: " << mode << ", Input: " << input << ", Output: " << output << endl;
    ofstream ofs(output);
    return 0;
}