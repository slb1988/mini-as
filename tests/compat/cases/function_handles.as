funcdef int Binary(int, int);

int add(int left, int right) {
    return left + right;
}

int multiply(int left, int right) {
    return left * right;
}

int apply(Binary@ callback, int left, int right) {
    return callback(left, right);
}

int main() {
    Binary@ callback = @add;
    int result = callback(10, 12);
    @callback = @multiply;
    return result + apply(callback, 4, 5);
}
