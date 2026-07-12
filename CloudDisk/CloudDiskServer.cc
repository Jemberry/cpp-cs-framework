#include "CloudDiskServer.h"
#include "CryptoUtil.h"
#include "common.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <wfrest/PathUtil.h>
#include <workflow/HttpUtil.h>
#include <workflow/MySQLResult.h>
#include <workflow/mysql_types.h>

using namespace std;
using namespace std::placeholders;
using namespace wfrest;
using namespace protocol;
using json = nlohmann::json;

static const string DatabaseURL = "mysql://root:1234@localhost/CloudDisk";
static const int RetryMax = 3;

void CloudDiskServer::register_routes()
{
    // 设置静态资源的路由
    register_www_module();
    // register_auth_module();
    // register_user_module();
    // register_file_module();
    // ...
}

void CloudDiskServer::register_www_module()
{
    server_.Static("/", "./www/index.html");
    server_.Static("/static", "./www/static");
}
