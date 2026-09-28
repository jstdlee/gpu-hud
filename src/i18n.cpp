#include "i18n.h"

#include <cstring>

const char* const kLangCodes[L_COUNT] = {"en", "zh_CN", "zh_TW", "ja", "ko", "es", "fr", "de", "ru", "pt"};
const char* const kLangNames[L_COUNT] = {"English", "简体中文", "繁體中文", "日本語", "한국어",
                                         "Español", "Français", "Deutsch", "Русский", "Português"};

namespace {
struct Row {
    Str id;
    const char* t[L_COUNT];  // en, zh_CN, zh_TW, ja, ko, es, fr, de, ru, pt
};

const Row kRows[] = {
    {S_GPU, {"GPU", "GPU", "GPU", "GPU", "GPU", "GPU", "GPU", "GPU", "ГП", "GPU"}},
    {S_GPU_MEM, {"VRAM", "显存", "顯存", "VRAM", "VRAM", "VRAM", "VRAM", "VRAM", "Видеопамять", "VRAM"}},
    {S_UNIFIED, {"GPU mem (unified)", "GPU 内存（统一）", "GPU 記憶體（統一）", "GPU メモリ（統合）", "GPU 메모리(통합)",
                 "Mem. GPU (unificada)", "Mém. GPU (unifiée)", "GPU-Speicher (vereint)", "Память ГП (общая)", "Mem. GPU (unificada)"}},
    {S_RAM, {"RAM", "内存", "記憶體", "RAM", "RAM", "RAM", "RAM", "RAM", "ОЗУ", "RAM"}},
    {S_CPU, {"CPU", "CPU", "CPU", "CPU", "CPU", "CPU", "CPU", "CPU", "ЦП", "CPU"}},
    {S_SWAP, {"Swap", "交换", "交換", "スワップ", "스왑", "Swap", "Swap", "Swap", "Подкачка", "Swap"}},
    {S_TOP_PROC, {"Top process", "占用最高进程", "佔用最高程序", "最大プロセス", "최상위 프로세스",
                  "Proceso principal", "Processus principal", "Top-Prozess", "Главный процесс", "Processo principal"}},
    {S_TOP_PROCS, {"GPU processes", "GPU 进程", "GPU 程序", "GPU プロセス", "GPU 프로세스",
                   "Procesos GPU", "Processus GPU", "GPU-Prozesse", "Процессы ГП", "Processos GPU"}},
    {S_NO_PROC, {"No GPU process", "无 GPU 进程", "無 GPU 程序", "GPU プロセスなし", "GPU 프로세스 없음",
                 "Sin procesos GPU", "Aucun processus GPU", "Kein GPU-Prozess", "Нет процессов ГП", "Nenhum processo GPU"}},
    {S_NO_GPU, {"No supported GPU detected", "未检测到支持的 GPU", "未偵測到支援的 GPU", "対応 GPU が見つかりません",
                "지원되는 GPU를 찾을 수 없음", "No se detectó una GPU compatible", "Aucun GPU compatible détecté",
                "Keine unterstützte GPU gefunden", "Поддерживаемый ГП не найден", "Nenhuma GPU compatível detectada"}},
    {S_PID, {"PID", "PID", "PID", "PID", "PID", "PID", "PID", "PID", "PID", "PID"}},
    {S_PROCESS, {"Process", "进程", "程序", "プロセス", "프로세스", "Proceso", "Processus", "Prozess", "Процесс", "Processo"}},
    {S_USER, {"User", "用户", "使用者", "ユーザー", "사용자", "Usuario", "Utilisateur", "Benutzer", "Пользователь", "Usuário"}},
    {S_MEM, {"Mem", "内存", "記憶體", "メモリ", "메모리", "Mem", "Mém", "Speicher", "Память", "Mem"}},
    {S_SM, {"SM", "SM", "SM", "SM", "SM", "SM", "SM", "SM", "SM", "SM"}},
    {S_COPY_ALL, {"Copy all", "复制全部", "複製全部", "すべてコピー", "모두 복사", "Copiar todo", "Tout copier",
                  "Alles kopieren", "Копировать всё", "Copiar tudo"}},
    {S_COPIED, {"Copied!", "已复制！", "已複製！", "コピーしました！", "복사됨!", "¡Copiado!", "Copié !", "Kopiert!",
                "Скопировано!", "Copiado!"}},
    {S_SETTINGS, {"Settings", "设置", "設定", "設定", "설정", "Ajustes", "Paramètres", "Einstellungen", "Настройки", "Configurações"}},
    {S_QUIT, {"Quit", "退出", "結束", "終了", "종료", "Salir", "Quitter", "Beenden", "Выход", "Sair"}},
    {S_CLOSE, {"Close", "关闭", "關閉", "閉じる", "닫기", "Cerrar", "Fermer", "Schließen", "Закрыть", "Fechar"}},
    {S_LANGUAGE, {"Language", "语言", "語言", "言語", "언어", "Idioma", "Langue", "Sprache", "Язык", "Idioma"}},
    {S_APPEARANCE, {"Appearance", "外观", "外觀", "外観", "모양", "Apariencia", "Apparence", "Darstellung", "Внешний вид", "Aparência"}},
    {S_BG_OPACITY, {"Background opacity", "背景不透明度", "背景不透明度", "背景の不透明度", "배경 불투명도",
                    "Opacidad del fondo", "Opacité du fond", "Hintergrund-Deckkraft", "Непрозрачность фона", "Opacidade do fundo"}},
    {S_CONTENT_OPACITY, {"Content opacity", "内容不透明度", "內容不透明度", "内容の不透明度", "내용 불투명도",
                         "Opacidad del contenido", "Opacité du contenu", "Inhalt-Deckkraft", "Непрозрачность содержимого", "Opacidade do conteúdo"}},
    {S_BG_COLOR, {"Background color", "背景颜色", "背景顏色", "背景色", "배경색", "Color de fondo", "Couleur du fond",
                  "Hintergrundfarbe", "Цвет фона", "Cor do fundo"}},
    {S_ACCENT, {"Accent color", "强调色", "強調色", "アクセント色", "강조 색", "Color de acento", "Couleur d'accent",
                "Akzentfarbe", "Акцентный цвет", "Cor de destaque"}},
    {S_BG_IMAGE, {"Background image", "背景图片", "背景圖片", "背景画像", "배경 이미지", "Imagen de fondo", "Image de fond",
                  "Hintergrundbild", "Фоновое изображение", "Imagem de fundo"}},
    {S_BROWSE, {"Browse…", "浏览…", "瀏覽…", "参照…", "찾아보기…", "Examinar…", "Parcourir…", "Durchsuchen…", "Обзор…", "Procurar…"}},
    {S_CLEAR, {"Clear", "清除", "清除", "クリア", "지우기", "Quitar", "Effacer", "Entfernen", "Убрать", "Limpar"}},
    {S_IMG_MODE, {"Image mode", "图片模式", "圖片模式", "表示方法", "이미지 모드", "Modo de imagen", "Mode d'image",
                  "Bildmodus", "Режим", "Modo da imagem"}},
    {S_TILE, {"Tile", "平铺", "並排", "タイル", "바둑판", "Mosaico", "Mosaïque", "Kacheln", "Плитка", "Lado a lado"}},
    {S_STRETCH, {"Stretch", "拉伸", "延展", "引き伸ばし", "늘이기", "Estirar", "Étirer", "Strecken", "Растянуть", "Esticar"}},
    {S_FILL, {"Fill", "填充", "填滿", "塗りつぶし", "채우기", "Rellenar", "Remplir", "Füllen", "Заполнить", "Preencher"}},
    {S_CENTER, {"Center", "居中", "置中", "中央", "가운데", "Centrar", "Centrer", "Zentriert", "По центру", "Centralizar"}},
    {S_IMG_SCALE, {"Image scale", "图片缩放", "圖片縮放", "画像の拡大率", "이미지 배율", "Escala de imagen", "Échelle de l'image",
                   "Bildskalierung", "Масштаб", "Escala da imagem"}},
    {S_IMG_OPACITY, {"Image opacity", "图片不透明度", "圖片不透明度", "画像の不透明度", "이미지 불투명도", "Opacidad de imagen",
                     "Opacité de l'image", "Bild-Deckkraft", "Непрозрачность изображения", "Opacidade da imagem"}},
    {S_DROP_HINT, {"Tip: drag & drop an image onto the window", "提示：可将图片拖放到窗口上", "提示：可將圖片拖放到視窗上",
                   "ヒント：画像をウィンドウにドロップできます", "팁: 이미지를 창에 끌어다 놓으세요",
                   "Consejo: arrastra una imagen a la ventana", "Astuce : glissez une image sur la fenêtre",
                   "Tipp: Bild auf das Fenster ziehen", "Совет: перетащите изображение в окно", "Dica: arraste uma imagem para a janela"}},
    {S_IMG_FAILED, {"Could not load image", "无法加载图片", "無法載入圖片", "画像を読み込めません", "이미지를 불러올 수 없음",
                    "No se pudo cargar la imagen", "Impossible de charger l'image", "Bild konnte nicht geladen werden",
                    "Не удалось загрузить изображение", "Não foi possível carregar a imagem"}},
    {S_BEHAVIOR, {"Behavior", "行为", "行為", "動作", "동작", "Comportamiento", "Comportement", "Verhalten", "Поведение", "Comportamento"}},
    {S_ALWAYS_ON_TOP, {"Always on top", "窗口置顶", "視窗置頂", "常に最前面", "항상 위", "Siempre visible", "Toujours au premier plan",
                       "Immer im Vordergrund", "Поверх всех окон", "Sempre no topo"}},
    {S_LOCK_POS, {"Lock position", "锁定位置", "鎖定位置", "位置を固定", "위치 고정", "Bloquear posición", "Verrouiller la position",
                  "Position sperren", "Закрепить положение", "Travar posição"}},
    {S_SHOW_PROCS, {"Show process list", "显示进程列表", "顯示程序清單", "プロセス一覧を表示", "프로세스 목록 표시",
                    "Mostrar lista de procesos", "Afficher la liste des processus", "Prozessliste anzeigen",
                    "Показывать список процессов", "Mostrar lista de processos"}},
    {S_SHOW_GRAPH, {"Show history graph", "显示历史曲线", "顯示歷史曲線", "履歴グラフを表示", "기록 그래프 표시",
                    "Mostrar gráfico", "Afficher l'historique", "Verlaufsgrafik anzeigen", "Показывать график", "Mostrar gráfico"}},
    {S_REFRESH, {"Refresh (ms)", "刷新间隔（毫秒）", "更新間隔（毫秒）", "更新間隔（ms）", "새로 고침(ms)", "Actualizar (ms)",
                 "Rafraîchir (ms)", "Aktualisierung (ms)", "Обновление (мс)", "Atualizar (ms)"}},
    {S_FONT_SIZE, {"Font size", "字体大小", "字型大小", "フォントサイズ", "글꼴 크기", "Tamaño de fuente", "Taille de police",
                   "Schriftgröße", "Размер шрифта", "Tamanho da fonte"}},
    {S_WIDTH, {"Width", "宽度", "寬度", "幅", "너비", "Ancho", "Largeur", "Breite", "Ширина", "Largura"}},
    {S_TEMP, {"Temp", "温度", "溫度", "温度", "온도", "Temp.", "Temp.", "Temp.", "Темп.", "Temp."}},
    {S_POWER, {"Power", "功耗", "功耗", "電力", "전력", "Potencia", "Puissance", "Leistung", "Мощность", "Potência"}},
    {S_CLOCK, {"Clock", "频率", "時脈", "クロック", "클럭", "Reloj", "Fréquence", "Takt", "Частота", "Clock"}},
    {S_FAN, {"Fan", "风扇", "風扇", "ファン", "팬", "Ventilador", "Ventilateur", "Lüfter", "Вентилятор", "Ventoinha"}},
    {S_ENC_DEC, {"Enc/Dec", "编/解码", "編/解碼", "エンコード/デコード", "인코딩/디코딩", "Cod/Decod", "Enc/Déc", "Enc/Dec",
                 "Код/Декод", "Cod/Decod"}},
    {S_REPORT_TITLE, {"GPU HUD system report", "GPU HUD 系统报告", "GPU HUD 系統報告", "GPU HUD システムレポート",
                      "GPU HUD 시스템 보고서", "Informe del sistema GPU HUD", "Rapport système GPU HUD",
                      "GPU-HUD-Systembericht", "Отчёт о системе GPU HUD", "Relatório do sistema GPU HUD"}},
    {S_LIVE, {"Live metrics", "实时指标", "即時指標", "リアルタイム指標", "실시간 지표", "Métricas en vivo", "Mesures en direct",
              "Live-Werte", "Текущие показатели", "Métricas ao vivo"}},
    {S_SYSTEM, {"System", "系统", "系統", "システム", "시스템", "Sistema", "Système", "System", "Система", "Sistema"}},
    {S_HOSTNAME, {"Hostname", "主机名", "主機名稱", "ホスト名", "호스트 이름", "Nombre de host", "Nom d'hôte", "Hostname",
                  "Имя хоста", "Nome do host"}},
    {S_OS, {"OS", "操作系统", "作業系統", "OS", "운영 체제", "SO", "SE", "Betriebssystem", "ОС", "SO"}},
    {S_KERNEL, {"Kernel", "内核", "核心", "カーネル", "커널", "Kernel", "Noyau", "Kernel", "Ядро", "Kernel"}},
    {S_ARCH, {"Architecture", "架构", "架構", "アーキテクチャ", "아키텍처", "Arquitectura", "Architecture", "Architektur",
              "Архитектура", "Arquitetura"}},
    {S_UPTIME, {"Uptime", "运行时间", "運行時間", "稼働時間", "가동 시간", "Tiempo activo", "Disponibilité", "Laufzeit",
                "Время работы", "Tempo ligado"}},
    {S_DESKTOP, {"Desktop", "桌面", "桌面", "デスクトップ", "데스크톱", "Escritorio", "Bureau", "Desktop", "Рабочий стол", "Área de trabalho"}},
    {S_MODEL, {"Model", "型号", "型號", "モデル", "모델", "Modelo", "Modèle", "Modell", "Модель", "Modelo"}},
    {S_VENDOR, {"Vendor", "厂商", "廠商", "ベンダー", "제조사", "Fabricante", "Fabricant", "Hersteller", "Производитель", "Fabricante"}},
    {S_CORES, {"Logical CPUs", "逻辑处理器", "邏輯處理器", "論理 CPU", "논리 CPU", "CPU lógicas", "CPU logiques", "Logische CPUs",
               "Логические ЦП", "CPUs lógicas"}},
    {S_MAX_FREQ, {"Max frequency", "最高频率", "最高頻率", "最大周波数", "최대 주파수", "Frecuencia máx.", "Fréquence max",
                  "Max. Frequenz", "Макс. частота", "Frequência máx."}},
    {S_MEMORY, {"Memory", "内存", "記憶體", "メモリ", "메모리", "Memoria", "Mémoire", "Arbeitsspeicher", "Память", "Memória"}},
    {S_TOTAL, {"Total", "总计", "總計", "合計", "전체", "Total", "Total", "Gesamt", "Всего", "Total"}},
    {S_MOTHERBOARD, {"Motherboard", "主板", "主機板", "マザーボード", "메인보드", "Placa base", "Carte mère", "Mainboard",
                     "Материнская плата", "Placa-mãe"}},
    {S_PRODUCT, {"Product", "产品", "產品", "製品", "제품", "Producto", "Produit", "Produkt", "Продукт", "Produto"}},
    {S_BOARD, {"Board", "主板型号", "主機板型號", "ボード", "보드", "Placa", "Carte", "Board", "Плата", "Placa"}},
    {S_BIOS, {"BIOS", "BIOS", "BIOS", "BIOS", "BIOS", "BIOS", "BIOS", "BIOS", "BIOS", "BIOS"}},
    {S_DRIVER, {"Driver", "驱动", "驅動程式", "ドライバー", "드라이버", "Controlador", "Pilote", "Treiber", "Драйвер", "Driver"}},
    {S_CUDA, {"CUDA", "CUDA", "CUDA", "CUDA", "CUDA", "CUDA", "CUDA", "CUDA", "CUDA", "CUDA"}},
    {S_PCI, {"PCI bus", "PCI 总线", "PCI 匯流排", "PCI バス", "PCI 버스", "Bus PCI", "Bus PCI", "PCI-Bus", "Шина PCI", "Barramento PCI"}},
    {S_UUID, {"UUID", "UUID", "UUID", "UUID", "UUID", "UUID", "UUID", "UUID", "UUID", "UUID"}},
    {S_POWER_LIMIT, {"Power limit", "功耗上限", "功耗上限", "電力制限", "전력 제한", "Límite de potencia", "Limite de puissance",
                     "Leistungslimit", "Предел мощности", "Limite de potência"}},
    {S_MAX_CLOCK, {"Max clock", "最高频率", "最高時脈", "最大クロック", "최대 클럭", "Reloj máx.", "Fréquence max", "Max. Takt",
                   "Макс. частота", "Clock máx."}},
    {S_DISPLAYS, {"Displays", "显示器", "顯示器", "ディスプレイ", "디스플레이", "Pantallas", "Écrans", "Bildschirme", "Мониторы", "Monitores"}},
    {S_STORAGE, {"Storage", "存储", "儲存裝置", "ストレージ", "저장 장치", "Almacenamiento", "Stockage", "Speicher", "Накопители", "Armazenamento"}},
    {S_RENDERER, {"OpenGL renderer", "OpenGL 渲染器", "OpenGL 繪製器", "OpenGL レンダラー", "OpenGL 렌더러", "Renderizador OpenGL",
                  "Moteur de rendu OpenGL", "OpenGL-Renderer", "Рендерер OpenGL", "Renderizador OpenGL"}},
    {S_USED, {"used", "已用", "已用", "使用中", "사용", "usado", "utilisé", "belegt", "занято", "usado"}},
};

const char* g_table[S_COUNT][L_COUNT];
Lang g_lang = L_EN;

struct Init {
    Init() {
        for (const Row& r : kRows)
            for (int l = 0; l < L_COUNT; l++) g_table[r.id][l] = r.t[l];
    }
} g_init;
}  // namespace

const char* tr(Str s) {
    const char* v = g_table[s][g_lang];
    if (!v) v = g_table[s][L_EN];
    return v ? v : "?";
}

void set_lang(Lang l) { g_lang = l; }
Lang get_lang() { return g_lang; }

Lang lang_from_code(const char* code) {
    if (!code || !*code) return L_EN;
    for (int l = 0; l < L_COUNT; l++)
        if (strcmp(code, kLangCodes[l]) == 0) return (Lang)l;
    if (strncmp(code, "zh", 2) == 0) {
        if (strstr(code, "TW") || strstr(code, "HK") || strstr(code, "MO") || strstr(code, "Hant")) return L_ZH_TW;
        return L_ZH_CN;
    }
    for (int l = 0; l < L_COUNT; l++)
        if (strncmp(code, kLangCodes[l], 2) == 0) return (Lang)l;
    return L_EN;
}
