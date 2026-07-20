#include <iostream>

using namespace std;

int main() {
  string str;

  str.append("\0\n\0\n");
  cout << str.size() << endl;
}