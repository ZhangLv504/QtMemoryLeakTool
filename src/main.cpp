#include <iostream>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

// 定义包含 id、name 和构造函数的 User
struct User
{
    int id;
    std::string name;

    User()
    {
        id = 0;
        name = "";
    }
    User(int id, std::string name) : id(id), name(std::move(name)) {}

};

int main()
{
    std::vector<User> users;

    // 用 push_back(alice) 插入已有对象
    User alice(1, "alice");
    users.push_back(alice);
    // 用 emplace_back(2, "Bob") 原地构造。
    users.emplace_back(2, "Bob");
    // 使用 matrix.push_back({1, 2, 3})。
    std::vector<std::vector<int>> matrix;
    matrix.push_back({1, 2 ,3});
    // 注释保留 matrix.emplace_back({4, 5, 6})，说明其不能推导类型。
    // C++ 中大括号初始化列表 {4, 5, 6} 本身是没有类型的，而 emplace_back 是一个模板函数，依赖编译器推导参数类型。
    // matrix.emplace_back({4, 5, 6});
    // 定义聚合类型 Point，使用 push_back({10, 20})。
    struct Point
    {
        int x;
        int y;
    };
    std::vector<Point> points;
    points.push_back({10, 20});

    // 遍历输出结果。
    for (const auto& user : users) {
        std::cout << user.id << " " << user.name << '\n';
    }

    for (const auto& row : matrix) {
        for (int value : row) {
            std::cout << value << " ";
        }
        std::cout << '\n';
    }

    for (const auto& point : points) {
        std::cout << point.x << " " << point.y << '\n';
    }

    return 0;
}