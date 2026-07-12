#pragma once
#include <wfrest/HttpServer.h>
#include <workflow/WFFacilities.h>

// 设计原则：组合优于继承
//     组合：有选择的复用代码
//     继承：会复用基类的所有代码
// 装饰器模式（Wrapper）
//     保持接口一致，可以降低用户的学习成本
class CloudDiskServer {
public:
    CloudDiskServer() { }

    // 注册路由
    void register_routes();

    int start(unsigned short port)
    {
        return server_.start(port);
    }

    void stop() { server_.stop(); }

    void list_routes() { server_.list_routes(); }

private:
    // 注册路由
    void register_www_module();
    void register_auth_module();
    void register_user_module();
    void register_file_module();

private:
    // 名字中最好不要带具体的实现细节
    // 方便以后修改具体的实现
    wfrest::HttpServer server_; // 组合
};
