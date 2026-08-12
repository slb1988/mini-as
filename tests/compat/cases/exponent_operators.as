int main() {
    int associated = 2 ** 3 ** 2;
    int compound = 3;
    compound **= 3;
    double reciprocal = 2.0 ** -1;
    int truncated = 2 ** -1;
    return associated == 64 && compound == 27 && reciprocal == 0.5 &&
           truncated == 0 ? 42 : 0;
}
