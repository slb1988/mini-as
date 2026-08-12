funcdef int Binary(int, int);

int apply(int left, int right, Binary@ operation) {
    return operation(left, right);
}

int main() {
    return apply(6, 6, function(left, right) {
        return left * right + left;
    });
}
