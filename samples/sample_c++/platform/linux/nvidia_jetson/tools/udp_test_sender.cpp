/**
 ********************************************************************
 * @file    udp_test_sender.cpp
 * @brief   模拟无人机数据 UDP 发送器 - 用于测试 Python 接收端
 *
 * 编译方法 (Linux):
 *   g++ -o udp_test_sender udp_test_sender.cpp -std=c++11
 *
 * 编译方法 (Windows with MinGW):
 *   g++ -o udp_test_sender.exe udp_test_sender.cpp -std=c++11 -lws2_32
 *
 * 运行:
 *   ./udp_test_sender <目标IP> <端口>
 *   ./udp_test_sender 192.168.1.100 14550
 *
 *********************************************************************
 */

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <chrono>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define closesocket close
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

// 模拟数据结构
struct SimulatedDroneData {
    // 姿态
    double pitch;
    double roll;
    double yaw;
    
    // 位置
    double latitude;
    double longitude;
    double altitude;
    
    // 速度
    double vx, vy, vz;
    
    // 电池
    int voltage;
    int current;
    int percent;
    
    // 状态
    int flightStatus;
    int displayMode;
    float height;
    int satellites;
    int homeSet;
    
    // 四元数
    double q0, q1, q2, q3;
};

// 格式化为JSON
int formatToJson(const SimulatedDroneData* data, char* buffer, size_t bufferSize) {
    time_t now = time(NULL);
    
    return snprintf(buffer, bufferSize,
        "{"
        "\"timestamp\":%ld,"
        "\"attitude\":{"
            "\"pitch\":%.4f,"
            "\"roll\":%.4f,"
            "\"yaw\":%.4f"
        "},"
        "\"position\":{"
            "\"lat\":%.7f,"
            "\"lon\":%.7f,"
            "\"alt\":%.2f"
        "},"
        "\"position_fused\":{"
            "\"lat\":%.7f,"
            "\"lon\":%.7f,"
            "\"alt\":%.2f,"
            "\"satellites\":%d"
        "},"
        "\"velocity\":{"
            "\"vx\":%.3f,"
            "\"vy\":%.3f,"
            "\"vz\":%.3f"
        "},"
        "\"battery\":{"
            "\"voltage\":%d,"
            "\"current\":%d,"
            "\"percent\":%d,"
            "\"capacity\":5000"
        "},"
        "\"flight_status\":%d,"
        "\"display_mode\":%d,"
        "\"height\":%.2f,"
        "\"home_set\":%d,"
        "\"quaternion\":{"
            "\"q0\":%.6f,"
            "\"q1\":%.6f,"
            "\"q2\":%.6f,"
            "\"q3\":%.6f"
        "}"
        "}",
        (long)now,
        data->pitch,
        data->roll,
        data->yaw,
        data->latitude,
        data->longitude,
        data->altitude,
        data->latitude,
        data->longitude,
        data->altitude,
        data->satellites,
        data->vx,
        data->vy,
        data->vz,
        data->voltage,
        data->current,
        data->percent,
        data->flightStatus,
        data->displayMode,
        data->height,
        data->homeSet,
        data->q0,
        data->q1,
        data->q2,
        data->q3
    );
}

// 更新模拟数据
void updateSimulatedData(SimulatedDroneData* data, double t) {
    // 模拟飞行轨迹 - 圆形飞行
    double radius = 0.0005; // 约50米半径
    double centerLat = 22.542812;
    double centerLon = 113.958902;
    double angularSpeed = 0.1; // 弧度/秒
    
    // 位置 - 圆形轨迹
    data->latitude = centerLat + radius * sin(angularSpeed * t);
    data->longitude = centerLon + radius * cos(angularSpeed * t);
    data->altitude = 50.0 + 5.0 * sin(0.05 * t); // 高度波动
    data->height = data->altitude - 10.0;
    
    // 速度
    data->vx = 5.0 * cos(angularSpeed * t);
    data->vy = 5.0 * sin(angularSpeed * t);
    data->vz = 0.25 * cos(0.05 * t);
    
    // 姿态 - 模拟小幅度晃动
    data->pitch = 5.0 * sin(0.3 * t);
    data->roll = 3.0 * cos(0.4 * t);
    data->yaw = fmod(angularSpeed * t * 180.0 / 3.14159 + 180.0, 360.0) - 180.0;
    
    // 四元数 (简化计算)
    double halfYaw = data->yaw * 3.14159 / 360.0;
    data->q0 = cos(halfYaw);
    data->q1 = 0;
    data->q2 = 0;
    data->q3 = sin(halfYaw);
    
    // 电池 - 缓慢下降
    data->voltage = 48000 - (int)(t * 10);
    data->current = 8000 + rand() % 2000;
    data->percent = 100 - (int)(t / 60.0);
    if (data->percent < 20) data->percent = 20;
    
    // GPS卫星数
    data->satellites = 18 + rand() % 5;
    
    // 飞行状态
    data->flightStatus = 2; // 空中飞行
    data->displayMode = 6;  // GPS模式
    data->homeSet = 1;
}

void printUsage(const char* progName) {
    std::cout << "用法: " << progName << " <目标IP> [端口]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  目标IP   - 接收电脑的IP地址 (例如: 192.168.1.100)" << std::endl;
    std::cout << "  端口     - UDP端口 (默认: 14550)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << progName << " 127.0.0.1" << std::endl;
    std::cout << "  " << progName << " 192.168.1.100 14550" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    const char* targetIp = argv[1];
    int targetPort = (argc >= 3) ? atoi(argv[2]) : 14550;
    
    std::cout << "======================================" << std::endl;
    std::cout << "  模拟无人机 UDP 数据发送器" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "目标地址: " << targetIp << ":" << targetPort << std::endl;
    std::cout << "发送频率: 10 Hz" << std::endl;
    std::cout << "按 Ctrl+C 退出" << std::endl;
    std::cout << "--------------------------------------" << std::endl;
    
#ifdef _WIN32
    // Windows: 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[ERROR] WSAStartup 失败" << std::endl;
        return 1;
    }
#endif
    
    // 创建 UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[ERROR] 创建 socket 失败" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    
    // 设置目标地址
    struct sockaddr_in targetAddr;
    memset(&targetAddr, 0, sizeof(targetAddr));
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(targetPort);
    
    if (inet_pton(AF_INET, targetIp, &targetAddr.sin_addr) <= 0) {
        std::cerr << "[ERROR] 无效的IP地址: " << targetIp << std::endl;
        closesocket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    
    // 模拟数据
    SimulatedDroneData data;
    memset(&data, 0, sizeof(data));
    
    char buffer[2048];
    int packetCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    
    srand((unsigned int)time(NULL));
    
    std::cout << "\n[INFO] 开始发送模拟数据...\n" << std::endl;
    
    while (true) {
        // 计算运行时间
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        
        // 更新模拟数据
        updateSimulatedData(&data, elapsed);
        
        // 格式化为JSON
        int len = formatToJson(&data, buffer, sizeof(buffer));
        
        // 发送UDP数据包
        int sent = sendto(sock, buffer, len, 0, 
                         (struct sockaddr*)&targetAddr, sizeof(targetAddr));
        
        if (sent > 0) {
            packetCount++;
            
            // 每秒打印一次状态
            if (packetCount % 10 == 0) {
                printf("\r[发送] 包数: %d | 时间: %.1fs | 位置: (%.6f, %.6f) | 高度: %.1fm | 电量: %d%%   ",
                       packetCount, elapsed, data.latitude, data.longitude, 
                       data.altitude, data.percent);
                fflush(stdout);
            }
        } else {
            std::cerr << "\n[WARN] 发送失败" << std::endl;
        }
        
        // 等待100ms (10Hz)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    
    return 0;
}
