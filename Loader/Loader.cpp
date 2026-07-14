#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <algorithm>

// Функция для перевода строки в нижний регистр (для нечувствительного к регистру сравнения)
std::wstring ToLower(std::wstring str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

// Поиск ID процесса без учета регистра букв
DWORD GetProcessIdByName(const std::wstring& processName) {
    DWORD processId = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(snapshot, &entry)) {
            std::wstring targetName = ToLower(processName);
            do {
                if (targetName == ToLower(entry.szExeFile)) {
                    processId = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return processId;
}

// Функция инжекта DLL в процесс
bool InjectDLL(DWORD processId, const std::string& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) {
        std::cerr << "[!] Не удалось открыть процесс игры. Запусти лоадер от Администратора!" << std::endl;
        return false;
    }

    void* pDllPathInMemory = VirtualAllocEx(hProcess, nullptr, dllPath.length() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPathInMemory) {
        std::cerr << "[!] Не удалось выделить память в процессе игры." << std::endl;
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pDllPathInMemory, dllPath.c_str(), dllPath.length() + 1, nullptr)) {
        std::cerr << "[!] Не удалось записать путь DLL в память игры." << std::endl;
        VirtualFreeEx(hProcess, pDllPathInMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    FARPROC pLoadLibraryA = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!pLoadLibraryA) {
        std::cerr << "[!] Не удалось найти функцию LoadLibraryA." << std::endl;
        VirtualFreeEx(hProcess, pDllPathInMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryA, pDllPathInMemory, 0, nullptr);
    if (!hThread) {
        std::cerr << "[!] Не удалось запустить поток в игре." << std::endl;
        VirtualFreeEx(hProcess, pDllPathInMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pDllPathInMemory, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

int main() {
    // Настраиваем консоль под правильный вывод русских символов (OEM CP-866)
    SetConsoleCP(866);
    SetConsoleOutputCP(866);
    setlocale(LC_ALL, ".866");

    std::cout << "===========================================" << std::endl;
    std::cout << "        WingsVisual Auto Loader v1.0        " << std::endl;
    std::cout << "===========================================" << std::endl;

    std::cout << "[*] Ищу запущенный процесс WoW..." << std::endl;

    // Ищем процесс (теперь найдет любой вариант: Wow.exe, WoW.exe, wow.exe)
    DWORD pid = GetProcessIdByName(L"wow.exe");

    if (pid == 0) {
        std::cout << "[!] Игра WoW не найдена! Пожалуйста, запусти игру перед запуском лоадера." << std::endl;
        std::cout << "Нажми Enter для выхода..." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "[+] Процесс игры успешно найден! PID: " << pid << std::endl;

    // Получаем путь к DLL
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string currentPath(buffer);
    std::string directory = currentPath.substr(0, currentPath.find_last_of("\\/"));
    std::string dllPath = directory + "\\WingsVisual.dll";

    std::cout << "[*] Путь к читу: " << dllPath << std::endl;

    // Проверяем наличие файла WingsVisual.dll
    DWORD dwAttrib = GetFileAttributesA(dllPath.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES || (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        std::cout << "[!] Ошибка: Файл WingsVisual.dll не найден в папке с лоадером!" << std::endl;
        std::cout << "Нажми Enter для выхода..." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "[*] Загружаю WingsVisual в игру..." << std::endl;
    if (InjectDLL(pid, dllPath)) {
        std::cout << "[+] WingsVisual успешно запущен! Приятной игры!" << std::endl;
        Beep(523, 200); // Сигнал успеха
        Beep(659, 200);
    }
    else {
        std::cout << "[!] Не удалось загрузить DLL. Попробуй запустить лоадер от Администратора." << std::endl;
        std::cout << "Нажми Enter для выхода..." << std::endl;
        std::cin.get();
    }

    Sleep(1500);
    return 0;
}