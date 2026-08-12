int main() {
    dictionary@ values = dictionary();
    values.set("a", int64(20));
    values.set("b", int64(21));
    values.set("c", int64(1));
    int64 read = 0;
    bool found = values.get("a", read);
    int64 total = 0;
    foreach (auto value, auto key : values) {
        total += int64(value);
    }
    bool erased = values.delete("c");
    bool valid = found && erased && read == 20 && values.getSize() == 2 &&
                 !values.exists("c");
    return valid ? int(total) : 0;
}
