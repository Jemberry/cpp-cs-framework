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
    register_auth_module();
    register_user_module();
    register_file_module();
    // ...
}

void CloudDiskServer::register_www_module()
{
    server_.Static("/", "./www/index.html");
    server_.Static("/static", "./www/static");
}

static void login_callback(HttpResp* resp, string password, WFMySQLTask* task)
{
    json result;
    /* MySQL任务失败 */
    if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->get_packet_type() == MYSQL_PACKET_ERROR) {
        resp->set_status(HttpStatusInternalServerError);
        result["status"] = "error";
        result["message"] = "内部服务器错误";
        resp->Json(result.dump());
        return;
    }

    // MySQL任务执行成功
    MySQLResultCursor cursor(task->get_resp());
    vector<MySQLCell> record;

    if (!cursor.fetch_row(record)) {
        /* 用户不存在：空结果集 */
        resp->set_status(HttpStatusUnauthorized);
        result["status"] = "error";
        result["message"] = "用户名或密码错误";
        resp->Json(result.dump());
        return;
    }

    User user;
    user.id = record[0].as_int();
    user.username = record[1].as_string();
    user.password = record[2].as_string();
    user.salt = record[3].as_string();
    user.createdAt = record[4].as_datetime();

    string hashcode = CryptoUtil::hash_password(password, user.salt);
    if (hashcode != user.password) {
        // 密码错误
        resp->set_status(HttpStatusUnauthorized);
        result["status"] = "error";
        result["message"] = "用户名或密码错误";
        resp->Json(result.dump());
    } else {
        // 密码正确
        resp->set_status(HttpStatusOK); // 200
        result["status"] = "success";
        result["message"] = "登录成功";
        result["data"]["accessToken"] = CryptoUtil::generate_token(user);
        result["data"]["tokenType"] = "Bearer";
        result["data"]["user"]["userId"] = user.id;
        result["data"]["user"]["username"] = user.username;
        resp->Json(result.dump());
    }
}

void CloudDiskServer::register_auth_module()
{
    server_.POST("/api/v1/auth/register", [](const HttpReq* req, HttpResp* resp) {
        json result;
        // 1. 校验请求类型
        if (req->content_type() != APPLICATION_JSON) {
            resp->set_status(HttpStatusBadRequest); // 400
            result["status"] = "error";
            result["message"] = "请求格式有误";
            resp->Json(result.dump());
            return;
        }
        // 2. 解析请求，校验请求参数
        json userinfo = json::parse(req->body());
        string username = userinfo["username"];
        string password = userinfo["password"];
        string confirm = userinfo["confirm"];
        if (username == "" || password == "") {
            resp->set_status(HttpStatusBadRequest); // 400
            result["status"] = "error";
            result["message"] = "用户名和密码不能为空";
            resp->Json(result.dump());
            return;
        }
        if (password != confirm) {
            resp->set_status(HttpStatusBadRequest); // 400
            result["status"] = "error";
            result["message"] = "两次输入的密码不一致";
            resp->Json(result.dump());
            return;
        }
        // 3. 注册：创建MySQL任务，将用户名和密码插入到tbl_user表
        string salt = CryptoUtil::generate_salt();
        string hashcode = CryptoUtil::hash_password(password, salt);
        string sql = "INSERT INTO tbl_user (username, password, salt) VALUES ('"
            + username + "', '"
            + hashcode + "', '"
            + salt + "')";
        cout << "[SQL] " << sql << endl; /* 调试信息：拼接SQL很容易出错 */

        // 注意：这里必须使用值捕获，否则会出现段错误！因为MySQL任务是在这个Handler之后执行的。
        resp->MySQL(DatabaseURL, sql, [=](MySQLResultCursor* cursor) {
            json result;
            if (cursor->get_cursor_status() != MYSQL_STATUS_OK) {
                resp->set_status(HttpStatusConflict); // 409
                result["status"] = "error";
                result["message"] = "用户名已存在";
                resp->Json(result.dump());
                return;
            }
            resp->set_status(HttpStatusCreated);
            result["status"] = "success";
            result["message"] = "注册成功";
            result["data"]["userId"] = cursor->get_insert_id();
            result["data"]["username"] = username;
            resp->Json(result.dump());
        });
    });

    server_.POST("/api/v1/auth/login", [](const HttpReq* req, HttpResp* resp, SeriesWork* series) {
        json result;
        // 1. 校验请求类型
        if (req->content_type() != APPLICATION_JSON) {
            resp->set_status(HttpStatusBadRequest); // 400
            result["status"] = "error";
            result["message"] = "请求格式有误";
            resp->Json(result.dump());
            return;
        }
        // 2. 解析请求，校验请求参数
        json userinfo = json::parse(req->body());
        string username = userinfo["username"];
        string password = userinfo["password"];
        if (username == "" || password == "") {
            resp->set_status(HttpStatusBadRequest); // 400
            result["status"] = "error";
            result["message"] = "用户名和密码不能为空";
            resp->Json(result.dump());
            return;
        }
        // 3. 登录：创建MySQL任务，查询tbl_user表
        string sql = "SELECT * FROM tbl_user WHERE username='"
            + username + "' AND tomb=0";
        cout << "[SQL] " << sql << endl; /* 调试信息 */

        WFMySQLTask* task = WFTaskFactory::create_mysql_task(
            DatabaseURL,
            RetryMax,
            std::bind(login_callback, resp, password, _1));

        task->get_req()->set_query(sql);
        series->push_back(task);
    });
}

void CloudDiskServer::register_user_module()
{
    server_.GET("/api/v1/user/me", [](const HttpReq* req, HttpResp* resp) {
        json result;
        const string& auth = req->header("Authorization");
        if (auth.find_first_of("Bearer ") != 0) {
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        // 校验token
        string token = auth.substr(7);
        User user;
        if (!CryptoUtil::verify_token(token, user)) {
            // 校验失败
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        // 校验成功
        resp->set_status(HttpStatusOK);
        result["status"] = "success";
        result["message"] = "获取个人信息成功";
        result["data"]["userId"] = user.id;
        result["data"]["username"] = user.username;
        result["data"]["createdAt"] = user.createdAt;
        resp->Json(result.dump());
    });
}

void CloudDiskServer::register_file_module()
{
    server_.GET("/api/v1/files", [](const HttpReq* req, HttpResp* resp) {
        json result;
        // 1. 校验token
        const string& auth = req->header("Authorization");
        if (auth.find_first_of("Bearer ") != 0) {
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        string token = auth.substr(7);
        User user;
        if (!CryptoUtil::verify_token(token, user)) {
            // 校验失败
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        // 2. 构建SQL语句，查询数据库
        string sql = "SELECT id, filename, size, created_at, last_update FROM tbl_file WHERE uid="
            + std::to_string(user.id);
        cout << "[SQL] " << sql << endl;
        resp->MySQL(DatabaseURL, sql, [=](MySQLResultCursor* cursor) {
            json result;
            if (cursor->get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
                resp->set_status(HttpStatusInternalServerError);
                result["status"] = "error";
                result["message"] = "内部服务器错误";
                resp->Json(result.dump());
                return;
            }
            // MySQL任务执行成功
            resp->set_status(HttpStatusOK);
            result["status"] = "success";
            result["message"] = "获取文件列表成功";
            result["data"]["files"] = json::array();
            vector<MySQLCell> record;
            while (cursor->fetch_row(record)) {
                json file;
                file["fileId"] = record[0].as_int();
                file["filename"] = record[1].as_string();
                file["size"] = record[2].as_int();
                file["createdAt"] = record[3].as_datetime();
                file["updatedAt"] = record[3].as_datetime();
                result["data"]["files"].push_back(std::move(file));
            }
            resp->Json(result.dump());
        });
    });

    server_.POST("/api/v1/files", [](const HttpReq* req, HttpResp* resp) {
        json result;
        // 1. 校验token
        const string& auth = req->header("Authorization");
        if (auth.find_first_of("Bearer ") != 0) {
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        string token = auth.substr(7);
        User user;
        if (!CryptoUtil::verify_token(token, user)) {
            // 校验失败
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        // 2. 处理文件
        if (req->content_type() != MULTIPART_FORM_DATA) {
            resp->set_status(HttpStatusBadRequest); // 400
            result["status"] = "error";
            result["message"] = "请求格式有误";
            resp->Json(result.dump());
            return;
        }
        Form& form = req->form();
        for (const auto& [key, file] : form) {
            const string& filename = file.first;
            const string& content = file.second;
            string hashcode = CryptoUtil::generate_hashcode(content.c_str(), content.size());
            // 为每个用户单独创建一个文件夹
            string directory = "upload_files/" + user.username + "/";

            // access测试文件是否可以访问, F_OK用来判断文件是否存在
            if (access(directory.c_str(), F_OK) == -1) {
                // 如果文件夹不存在，则创建文件夹
                mkdir(directory.c_str(), 0777);
            }

            string filepath = directory + PathUtil::base(filename);
            resp->Save(filepath, std::move(content));

            // 写`tbl_file`表
            string sql = "REPLACE INTO tbl_file (uid, filename, hashcode, size) VALUES ("
                + std::to_string(user.id) + ", '"
                + filename + "', '"
                + hashcode + "', "
                + std::to_string(content.size()) + ")";

            cout << "[SQL] " << sql << endl;

            resp->MySQL(DatabaseURL, sql, [=](MySQLResultCursor* cursor) {
                json result;
                if (cursor->get_cursor_status() != MYSQL_STATUS_OK) {
                    resp->set_status(HttpStatusInternalServerError); // 500
                    result["status"] = "error";
                    result["message"] = "内部服务器错误";
                    resp->Json(result.dump());
                } else {
                    resp->set_status(HttpStatusCreated);
                    result["status"] = "success";
                    result["message"] = "上传成功";
                    result["data"]["fileId"] = cursor->get_insert_id();
                    result["data"]["filename"] = filename;
                    resp->Json(result.dump());
                }
            });
        }
    });

    server_.GET("/api/v1/file/{id}", [](const HttpReq* req, HttpResp* resp) {
        json result;
        // 1. 校验token
        const string& auth = req->header("Authorization");
        if (auth.find_first_of("Bearer ") != 0) {
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        string token = auth.substr(7);
        User user;
        if (!CryptoUtil::verify_token(token, user)) {
            // 校验失败
            resp->set_status(HttpStatusUnauthorized); // 401
            result["status"] = "error";
            result["message"] = "无效的访问令牌";
            resp->Json(result.dump());
            return;
        }
        // 2. 查询数据库，获取文件的信息
        const string& id = req->param("id");
        string sql = "SELECT filename FROM tbl_file WHERE uid="
            + std::to_string(user.id)
            + " AND id=" + id;
        cout << "[SQL] " << sql << endl;

        resp->MySQL(DatabaseURL, sql, [=](MySQLResultCursor* cursor) {
            json result;
            if (cursor->get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
                resp->set_status(HttpStatusInternalServerError); // 500
                result["status"] = "error";
                result["message"] = "内部服务器错误";
                resp->Json(result.dump());
                return;
            }
            if (cursor->get_rows_count() == 0) {
                resp->set_status(HttpStatusNotFound); // 404
                result["status"] = "error";
                result["message"] = "文件不存在";
                resp->Json(result.dump());
                return;
            }
            vector<MySQLCell> record;
            cursor->fetch_row(record);
            string filename = record[0].as_string();
            resp->set_status(HttpStatusOK);
            resp->set_header_pair("Content-Disposition", "attachment; filename=" + filename);
            string filepath = "upload_files/" + user.username + "/" + filename;
            resp->File(filepath);
        });
    });
}
