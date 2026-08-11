int main() {
    any@ box = any(int64(40));
    int64 stored = 0;
    bool loaded = box.retrieve(stored);
    box.store(double(2));
    double extra = 0;
    bool loadedExtra = box.retrieve(extra);
    ref first;
    ref second;
    bool refs = first == second;
    return loaded && loadedExtra && refs ? int(stored + int64(extra)) : 0;
}
