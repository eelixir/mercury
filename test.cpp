#include "snap/snap.hpp"
struct Trade { int a; };
int main() {
    auto link = snap::connect<Trade>("shm://test");
    return 0;
}
