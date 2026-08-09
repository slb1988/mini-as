int finalized = 0;

class Resource {
    ~Resource() { finalized++; }
}

void dispose() {
    Resource@ value = Resource();
}

int main() {
    dispose();
    return finalized + 41;
}
