#include<iostream>
#include<vector>
#include<iterator>
#include<algorithm>
#include<time.h>
int main(){
    double start, end;
    start = clock();
    
    end = clock();
    std::cout << end - start << std::endl;

    // std::vector <int> v = {1, 2, 3};
    // auto ss = std::find_if(v.begin(), v.end(), [](int i){ return i%2 == 0; });
    // std::cout << *ss << std::endl;
    return 0;
}