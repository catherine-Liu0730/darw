// draw.cpp (Cross-platform: macOS / Windows)
// UI: rlutil.h (colors, locate, cls)
// Features:
// - Mode A: List draw (manual input / file load) + no-repeat + reset + status + save result
// - Mode B: Range draw (1..N) + optional no-repeat pool + reset + status
// Build macOS/Linux:   g++ draw.cpp -std=c++17 -O2 -o draw
// Run macOS/Linux:     ./draw
// Build Windows(MinGW): g++ draw.cpp -std=c++17 -O2 -o draw.exe
// Run Windows:          draw.exe

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <random>
#include <ctime>
#include <algorithm>

#include "rlutil.h"

#ifdef _WIN32
  #include <windows.h>
  static void setup_console_utf8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
  }
#else
  static void setup_console_utf8() {}
#endif

using namespace std;

// ---------------------- UI helpers ----------------------
static inline string trim(const string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void pause_anykey(const string& msg = "按任意鍵繼續...") {
  rlutil::setColor(rlutil::LIGHTGREEN);
  cout << "\n" << msg << flush;
  rlutil::setColor(rlutil::GREY);
  rlutil::anykey();
  cout << "\n";
}

static void clear_input_line() {
  // consume the rest of current line
  cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

static void draw_box(int x, int y, int w, int h) {
  // simple ASCII box
  rlutil::locate(x, y);
  cout << "+" << string(w - 2, '-') << "+";
  for (int i = 1; i <= h - 2; i++) {
    rlutil::locate(x, y + i);
    cout << "|" << string(w - 2, ' ') << "|";
  }
  rlutil::locate(x, y + h - 1);
  cout << "+" << string(w - 2, '-') << "+";
}

static void print_centered(int x, int y, int w, const string& s) {
  // best-effort centering (ASCII-based; CJK width may not be exact)
  int pad = (w - (int)s.size()) / 2;
  pad = clampi(pad, 0, w);
  rlutil::locate(x + 1 + pad, y);
  cout << s;
}

static void ui_header(const string& title, const string& subtitle = "") {
  rlutil::cls();

  const int W = 70;
  const int H = 9;
  const int X = 4;
  const int Y = 2;

  rlutil::setColor(rlutil::LIGHTCYAN);
  draw_box(X, Y, W, H);

  rlutil::setColor(rlutil::YELLOW);
  print_centered(X, Y + 1, W, "文字模式抽籤系統  Draw System");

  rlutil::setColor(rlutil::LIGHTGREEN);
  print_centered(X, Y + 3, W, title);

  if (!subtitle.empty()) {
    rlutil::setColor(rlutil::GREY);
    print_centered(X, Y + 5, W, subtitle);
  }

  rlutil::setColor(rlutil::DARKGREY);
  rlutil::locate(X + 2, Y + H - 2);
  rlutil::setColor(rlutil::GREY);

  rlutil::locate(1, Y + H + 1);
}

static void ui_status_bar(const string& left, const string& right) {
  // a simple status line at bottom area
  rlutil::setColor(rlutil::DARKGREY);
  cout << "\n------------------------------------------------------------\n";
  rlutil::setColor(rlutil::GREY);
  cout << left;
  if (!right.empty()) {
    int spaces = 60 - (int)left.size();
    if (spaces < 1) spaces = 1;
    cout << string(spaces, ' ') << right;
  }
  cout << "\n";
}

static void ui_menu(const vector<string>& items, const string& prompt = "選項") {
  rlutil::setColor(rlutil::LIGHTCYAN);
  for (auto &it : items) cout << it << "\n";
  rlutil::setColor(rlutil::GREY);
  cout << "\n" << prompt << "： " << flush;
}

// ---------------------- Data helpers ----------------------
static void dedup_preserve_order(vector<string>& v) {
  vector<string> out;
  out.reserve(v.size());
  for (auto &x : v) {
    bool seen = false;
    for (auto &y : out) {
      if (x == y) { seen = true; break; }
    }
    if (!seen) out.push_back(x);
  }
  v.swap(out);
}

static void save_history_to_file(const vector<string>& history, const string& filename) {
  ofstream fout(filename);
  if (!fout) return;
  for (size_t i = 0; i < history.size(); i++) {
    fout << (i + 1) << "," << history[i] << "\n";
  }
}

// ---------------------- Animations ----------------------
static int animated_pick_index(const vector<string>& pool, mt19937& rng, const string& label = "抽籤中") {
  uniform_int_distribution<int> dist(0, (int)pool.size() - 1);

  rlutil::setColor(rlutil::LIGHTMAGENTA);
  cout << "按任意鍵開始抽籤..." << flush;
  rlutil::setColor(rlutil::GREY);
  rlutil::anykey();

  ui_header(label, "候選人快速切換中...");
  rlutil::setColor(rlutil::LIGHTCYAN);
  cout << "\n";

  int y = 14;
  for (int i = 0; i < 26; i++) {
    int idx = dist(rng);
    rlutil::locate(8, y);
    rlutil::setColor(rlutil::LIGHTCYAN);
    cout << ">>> ";
    rlutil::setColor(rlutil::WHITE);
    cout << pool[idx] << "                           " << flush;
    rlutil::msleep(45 + (i / 10) * 10);
  }

  return dist(rng);
}

static int animated_pick_number(int N, mt19937& rng, const string& label = "抽籤中") {
  uniform_int_distribution<int> dist(1, N);

  rlutil::setColor(rlutil::LIGHTMAGENTA);
  cout << "按任意鍵開始抽籤..." << flush;
  rlutil::setColor(rlutil::GREY);
  rlutil::anykey();

  ui_header(label, "號碼快速跳動中...");
  int y = 14;

  for (int i = 0; i < 32; i++) {
    rlutil::locate(8, y);
    rlutil::setColor(rlutil::LIGHTCYAN);
    cout << ">>> ";
    rlutil::setColor(rlutil::WHITE);
    cout << dist(rng) << "                           " << flush;
    rlutil::msleep(35 + (i / 12) * 10);
  }

  return dist(rng);
}

// ---------------------- Mode A: List draw ----------------------
static void mode_list_draw(mt19937& rng) {
  vector<string> all;
  vector<string> pool;
  vector<string> history;

  while (true) {
    ui_header("模式 A：名單抽籤（不重複）", "可手動輸入 / 讀檔；抽到會從池子移除");
    ui_status_bar(
      "狀態：全部 " + to_string(all.size()) + " 人 / 可抽 " + to_string(pool.size()) + " 人 / 已抽 " + to_string(history.size()) + " 人",
      "A 模式"
    );

    ui_menu({
      "1) 手動輸入名單（逐行輸入，空行結束）",
      "2) 從檔案載入名單（每行一個名字）",
      "3) 抽一位（不重複）",
      "4) 查看名單（全部 / 剩餘 / 已抽）",
      "5) 重置抽籤（已抽回池子）",
      "6) 匯出已抽結果（CSV）",
      "0) 返回主選單"
    });

    int op;
    cin >> op;

    if (op == 0) return;

    if (op == 1) {
      ui_header("手動輸入名單", "一行一個名字；輸入空行結束");
      clear_input_line();

      string line;
      int added = 0;
      while (true) {
        rlutil::setColor(rlutil::LIGHTCYAN);
        cout << "> " << flush;
        rlutil::setColor(rlutil::GREY);

        getline(cin, line);
        line = trim(line);
        if (line.empty()) break;

        all.push_back(line);
        pool.push_back(line);
        added++;
      }

      dedup_preserve_order(all);
      dedup_preserve_order(pool);

      rlutil::setColor(rlutil::LIGHTGREEN);
      cout << "\n新增 " << added << " 筆；目前可抽 " << pool.size() << " 人。\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
    else if (op == 2) {
      ui_header("從檔案載入名單", "每行一個名字，例如 names.txt / classA.txt");
      cout << "請輸入檔名/路徑： " << flush;

      string filename;
      cin >> filename;

      ifstream fin(filename);
      if (!fin) {
        rlutil::setColor(rlutil::LIGHTRED);
        cout << "\n❌ 無法開啟檔案：" << filename << "\n";
        rlutil::setColor(rlutil::GREY);
        pause_anykey();
        continue;
      }

      string line;
      int added = 0;
      while (getline(fin, line)) {
        line = trim(line);
        if (line.empty()) continue;
        all.push_back(line);
        pool.push_back(line);
        added++;
      }

      dedup_preserve_order(all);
      dedup_preserve_order(pool);

      rlutil::setColor(rlutil::LIGHTGREEN);
      cout << "\n已載入 " << added << " 筆；目前可抽 " << pool.size() << " 人。\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
    else if (op == 3) {
      if (pool.empty()) {
        ui_header("抽一位", "池子已空，請先輸入名單或重置");
        rlutil::setColor(rlutil::LIGHTRED);
        cout << "⚠️ 沒有人可以抽。\n";
        rlutil::setColor(rlutil::GREY);
        pause_anykey();
        continue;
      }

      int idx = animated_pick_index(pool, rng, "抽籤中（名單）");
      string winner = pool[idx];

      pool.erase(pool.begin() + idx);
      history.push_back(winner);

      ui_header("抽籤結果", "恭喜中籤！");
      rlutil::setColor(rlutil::LIGHTGREEN);
      cout << "\n🎉 中籤：";
      rlutil::setColor(rlutil::YELLOW);
      cout << winner << "\n";
      rlutil::setColor(rlutil::GREY);
      cout << "剩餘可抽： " << pool.size() << " 人\n";

      pause_anykey();
    }
    else if (op == 4) {
      ui_header("查看名單", "可查看：全部 / 剩餘 / 已抽");
      ui_menu({
        "1) 全部名單",
        "2) 剩餘可抽",
        "3) 已抽記錄",
        "0) 返回"
      }, "選項");

      int t; cin >> t;
      if (t == 0) continue;

      auto print_list = [&](const vector<string>& v, const string& emptyMsg) {
        cout << "\n";
        if (v.empty()) {
          rlutil::setColor(rlutil::DARKGREY);
          cout << emptyMsg << "\n";
          rlutil::setColor(rlutil::GREY);
          return;
        }
        rlutil::setColor(rlutil::WHITE);
        for (size_t i = 0; i < v.size(); i++) cout << (i + 1) << ". " << v[i] << "\n";
        rlutil::setColor(rlutil::GREY);
      };

      if (t == 1) print_list(all, "（目前沒有任何名單）");
      else if (t == 2) print_list(pool, "（池子已空）");
      else if (t == 3) print_list(history, "（尚未抽出任何人）");

      pause_anykey();
    }
    else if (op == 5) {
      pool = all;
      history.clear();
      ui_header("重置完成", "已將已抽回池子");
      rlutil::setColor(rlutil::LIGHTGREEN);
      cout << "可抽：" << pool.size() << " 人\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
    else if (op == 6) {
      ui_header("匯出已抽結果", "輸出 CSV：序號,名字");
      cout << "輸出檔名（例如 result.csv）： " << flush;
      string out;
      cin >> out;
      save_history_to_file(history, out);

      rlutil::setColor(rlutil::LIGHTGREEN);
      cout << "\n✅ 已輸出（若 history 為空則為空檔）： " << out << "\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
    else {
      rlutil::setColor(rlutil::LIGHTRED);
      cout << "\n無效選項。\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
  }
}

// ---------------------- Mode B: Range draw ----------------------
static void mode_range_draw(mt19937& rng) {
  int N = 0;
  bool noRepeat = true;
  vector<int> pool;     // for no-repeat
  vector<int> history;  // drawn numbers

  auto reset_pool = [&]() {
    pool.clear();
    history.clear();
    if (N <= 0) return;
    pool.reserve(N);
    for (int i = 1; i <= N; i++) pool.push_back(i);
  };

  while (true) {
    ui_header("模式 B：範圍抽籤（1 ~ N）", "可選是否不重複抽；有重置與狀態顯示");
    ui_status_bar(
      "狀態：N=" + to_string(N) +
      " / 不重複=" + string(noRepeat ? "是" : "否") +
      " / 可抽=" + (noRepeat ? to_string((int)pool.size()) : string("-")) +
      " / 已抽=" + to_string((int)history.size()),
      "B 模式"
    );

    ui_menu({
      "1) 設定 N",
      "2) 切換不重複（目前：" + string(noRepeat ? "是" : "否") + "）",
      "3) 抽一次",
      "4) 查看已抽記錄",
      "5) 重置（清空已抽/重建池子）",
      "0) 返回主選單"
    });

    int op;
    cin >> op;
    if (op == 0) return;

    if (op == 1) {
      ui_header("設定 N", "例如 50 代表抽 1~50");
      cout << "請輸入 N： " << flush;
      cin >> N;
      if (N <= 0) {
        rlutil::setColor(rlutil::LIGHTRED);
        cout << "\nN 必須 > 0\n";
        rlutil::setColor(rlutil::GREY);
        pause_anykey();
        N = 0;
        pool.clear();
        history.clear();
        continue;
      }
      reset_pool();
      rlutil::setColor(rlutil::LIGHTGREEN);
      cout << "\n✅ 已設定 N=" << N << "\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
    else if (op == 2) {
      noRepeat = !noRepeat;
      if (noRepeat) reset_pool();
      pause_anykey(string("已切換不重複為：") + (noRepeat ? "是" : "否"));
    }
    else if (op == 3) {
      if (N <= 0) {
        ui_header("抽一次", "請先設定 N");
        rlutil::setColor(rlutil::LIGHTRED);
        cout << "⚠️ 你還沒設定 N。\n";
        rlutil::setColor(rlutil::GREY);
        pause_anykey();
        continue;
      }

      if (noRepeat) {
        if (pool.empty()) {
          ui_header("抽一次", "池子已空，請重置或關閉不重複");
          rlutil::setColor(rlutil::LIGHTRED);
          cout << "⚠️ 沒有號碼可抽。\n";
          rlutil::setColor(rlutil::GREY);
          pause_anykey();
          continue;
        }

        // show animation using N, but final pick must come from pool
        // We'll animate numbers, then pick from pool uniformly:
        animated_pick_number(N, rng, "抽籤中（號碼）");
        uniform_int_distribution<int> dist(0, (int)pool.size() - 1);
        int idx = dist(rng);
        int result = pool[idx];
        pool.erase(pool.begin() + idx);
        history.push_back(result);

        ui_header("抽籤結果", "恭喜中籤！");
        rlutil::setColor(rlutil::LIGHTGREEN);
        cout << "\n🎉 中籤號碼：";
        rlutil::setColor(rlutil::YELLOW);
        cout << result << "\n";
        rlutil::setColor(rlutil::GREY);
        cout << "剩餘可抽： " << pool.size() << "\n";

        pause_anykey();
      } else {
        int result = animated_pick_number(N, rng, "抽籤中（號碼）");
        history.push_back(result);

        ui_header("抽籤結果", "（此模式允許重複）");
        rlutil::setColor(rlutil::LIGHTGREEN);
        cout << "\n🎉 中籤號碼：";
        rlutil::setColor(rlutil::YELLOW);
        cout << result << "\n";
        rlutil::setColor(rlutil::GREY);
        pause_anykey();
      }
    }
    else if (op == 4) {
      ui_header("已抽記錄（號碼）", "由小到大顯示（不改變抽籤順序）");
      if (history.empty()) {
        rlutil::setColor(rlutil::DARKGREY);
        cout << "（尚未抽出）\n";
        rlutil::setColor(rlutil::GREY);
      } else {
        vector<int> tmp = history;
        sort(tmp.begin(), tmp.end());
        rlutil::setColor(rlutil::WHITE);
        for (size_t i = 0; i < tmp.size(); i++) cout << tmp[i] << (i + 1 == tmp.size() ? "\n" : ", ");
        rlutil::setColor(rlutil::GREY);
      }
      pause_anykey();
    }
    else if (op == 5) {
      reset_pool();
      ui_header("已重置", "已清空已抽並重建池子");
      rlutil::setColor(rlutil::LIGHTGREEN);
      cout << "N=" << N << " / 可抽=" << (noRepeat ? to_string((int)pool.size()) : string("-")) << "\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
    else {
      rlutil::setColor(rlutil::LIGHTRED);
      cout << "\n無效選項。\n";
      rlutil::setColor(rlutil::GREY);
      pause_anykey();
    }
  }
}

// ---------------------- Main ----------------------
int main() {
  setup_console_utf8();

  // Avoid "black screen" / buffering confusion
  ios::sync_with_stdio(true);
  cin.tie(&cout);

  mt19937 rng((unsigned)time(nullptr));

  while (true) {
    ui_header("主選單", "選擇你要的抽籤模式");
    ui_menu({
      "1) 模式 A：名單抽籤（不重複、可讀檔/手動、可匯出）",
      "2) 模式 B：範圍抽籤（1~N、不重複可切換）",
      "0) 離開"
    });

    int op;
    cin >> op;

    if (op == 0) break;
    if (op == 1) mode_list_draw(rng);
    else if (op == 2) mode_range_draw(rng);
    else pause_anykey("無效選項，按任意鍵返回...");
  }

  rlutil::cls();
  rlutil::setColor(rlutil::LIGHTCYAN);
  cout << "程式結束。\n";
  rlutil::setColor(rlutil::GREY);
  return 0;
}
