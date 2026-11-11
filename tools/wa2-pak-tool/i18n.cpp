#include "i18n.hpp"

namespace wa2::paktool::ui {
namespace {
using Catalog = std::array<std::wstring_view, std::size_t(Text::count)>;
// Every language supplies every UI message. Archive diagnostics remain English
// after a localized error summary; resource names and paths are never translated.
constexpr std::array<Catalog, languages.size()> catalogs{{
    Catalog{{
        L"WA2 PAK Tool", L"PAK pack / unpack",
        L"Choose a folder to create a PAK, or a PAK to extract into a new folder.",
        L"Language", L"Choose folder → Pack PAK", L"Choose PAK → Unpack",
        L"Name encoding", L"Auto (manifest / new: Shift-JIS)", L"Japanese Shift-JIS / CP932", L"Chinese GBK / CP936",
        L"LZSS compression (slower; store unchanged if not smaller)",
        L"Keep .wa2pak-manifest and .wa2pak-cache for repacking.\r\nWrong names? Change encoding and extract again. No overwrites.",
        L"Ready", L"Cancel", L"Preparing…", L"Cancelling…",
        L"Cancelled. No incomplete output was published.",
        L"Choose the folder to pack", L"Choose an output location (a new subfolder will be created)",
        L"Choose a PAK to unpack", L"Save PAK (choose a new filename)", L"All files",
        L"Target already exists", L"Choose a new PAK filename to keep the existing file.",
        L"Packing complete", L"Extraction complete", L"Files",
        L"Checking PAK index…", L"Scanning source folder…", L"Packing", L"Extracting", L"Copying video PAK (no transcoding)…",
        L"Operation failed.", L"Could not read the input.", L"Could not write the output. Check free space and permissions.",
        L"The PAK or its manifest is damaged or unsupported.", L"The archive contains an unsafe path.",
        L"The filename or its encoding is not supported.", L"The archive exceeds a format or size limit.",
        L"The destination already exists. It was not overwritten.", L"Choose a valid destination outside the source folder.",
        L"Symbolic links and directory junctions are not supported.", L"The source changed during the operation. Try again.",
        L"Language changed for this session; the preference could not be saved.",
        L"Preserve original PAK data (extra disk space; reuse unchanged files)",
        L"Checking original data…", L"Copying original PAK data…", L"Original PAK restored byte for byte",
    }},
    Catalog{{
        L"WA2 PAK 工具", L"PAK 打包 / 解包",
        L"选择文件夹生成 PAK，或选择 PAK 解包到新文件夹。",
        L"语言 / Language", L"选择文件夹 → 打包 PAK", L"选择 PAK → 解包",
        L"文件名编码", L"自动保留（新包默认 Shift-JIS）", L"日文 Shift-JIS / CP932", L"中文 GBK / CP936",
        L"LZSS 压缩（较慢；无收益的文件保留原样）",
        L"重新打包时请保留 .wa2pak-manifest 和 .wa2pak-cache。\r\n文件名乱码时，可更改编码后重新解包。不会覆盖已有文件。",
        L"就绪", L"取消", L"准备中…", L"正在取消…", L"已取消，未生成不完整的目标文件。",
        L"选择要打包的文件夹", L"选择解包位置（会在其中新建子文件夹）", L"选择要解包的 PAK", L"保存 PAK（请选择新文件名）", L"所有文件",
        L"目标已存在", L"为保留已有文件，请选择一个新的 PAK 文件名。",
        L"打包完成", L"解包完成", L"文件数",
        L"正在检查 PAK 索引…", L"正在扫描源文件夹…", L"正在打包", L"正在解包", L"正在复制视频 PAK（不转码）…",
        L"操作失败。", L"无法读取输入文件。", L"无法写入输出，请检查剩余空间和权限。",
        L"PAK 或清单损坏，或使用了不支持的格式。", L"包内包含不安全的路径。", L"文件名或文件名编码不受支持。",
        L"已超过格式或大小上限。", L"目标已存在，未覆盖。", L"请选择源文件夹以外的有效输出位置。",
        L"不支持符号链接和目录联接。", L"操作期间源文件发生变化，请重试。", L"本次界面语言已切换，但无法保存语言偏好。",
        L"保留原始 PAK 数据（额外占用磁盘空间；复用未修改文件）",
        L"正在校验原始数据…", L"正在复制原始 PAK 数据…", L"已逐字节还原原始 PAK",
    }},
    Catalog{{
        L"WA2 PAK 工具", L"PAK 打包 / 解包",
        L"選擇資料夾建立 PAK，或選擇 PAK 解包至新資料夾。",
        L"語言 / Language", L"選擇資料夾 → 打包 PAK", L"選擇 PAK → 解包",
        L"檔名編碼", L"自動保留（新包預設 Shift-JIS）", L"日文 Shift-JIS / CP932", L"中文 GBK / CP936",
        L"LZSS 壓縮（較慢；未縮小的檔案保留原樣）",
        L"重新打包時請保留 .wa2pak-manifest 和 .wa2pak-cache。\r\n檔名亂碼時，可更改編碼後重新解包。不會覆寫既有檔案。",
        L"就緒", L"取消", L"準備中…", L"正在取消…", L"已取消，未產生不完整的輸出檔案。",
        L"選擇要打包的資料夾", L"選擇解包位置（將建立新的子資料夾）", L"選擇要解包的 PAK", L"儲存 PAK（請選擇新檔名）", L"所有檔案",
        L"目標已存在", L"為保留既有檔案，請選擇新的 PAK 檔名。",
        L"打包完成", L"解包完成", L"檔案數",
        L"正在檢查 PAK 索引…", L"正在掃描來源資料夾…", L"正在打包", L"正在解包", L"正在複製影片 PAK（不轉碼）…",
        L"操作失敗。", L"無法讀取輸入檔案。", L"無法寫入輸出，請檢查剩餘空間與權限。",
        L"PAK 或清單損毀，或使用了不支援的格式。", L"封存內含不安全的路徑。", L"不支援此檔名或檔名編碼。",
        L"已超過格式或大小上限。", L"目標已存在，未覆寫。", L"請選擇來源資料夾以外的有效輸出位置。",
        L"不支援符號連結與目錄接合點。", L"操作期間來源檔案發生變更，請重試。", L"本次介面語言已切換，但無法儲存語言偏好。",
        L"保留原始 PAK 資料（額外佔用磁碟空間；重用未修改檔案）",
        L"正在驗證原始資料…", L"正在複製原始 PAK 資料…", L"已逐位元組還原原始 PAK",
    }},
    Catalog{{
        L"WA2 PAK ツール", L"PAK の作成 / 展開",
        L"フォルダーから PAK を作成、または PAK を新しいフォルダーへ展開します。",
        L"言語 / Language", L"フォルダー選択 → PAK 作成", L"PAK 選択 → 展開",
        L"ファイル名の文字コード", L"自動（新規は Shift-JIS）", L"日本語 Shift-JIS / CP932", L"中国語 GBK / CP936",
        L"LZSS 圧縮（時間がかかります。小さくなる場合のみ圧縮）",
        L"再作成には .wa2pak-manifest と .wa2pak-cache を残してください。\r\n文字化けする場合は文字コードを変更して再展開。上書きはしません。",
        L"準備完了", L"キャンセル", L"準備中…", L"キャンセル中…", L"キャンセルしました。不完全な出力は作成していません。",
        L"PAK にするフォルダーを選択", L"出力先を選択（新しいサブフォルダーを作成します）", L"展開する PAK を選択", L"PAK を保存（新しい名前を指定）", L"すべてのファイル",
        L"出力先が既に存在します", L"既存のファイルを残すため、新しい PAK ファイル名を指定してください。",
        L"PAK 作成完了", L"展開完了", L"ファイル数",
        L"PAK の索引を確認中…", L"フォルダーを検索中…", L"作成中", L"展開中", L"動画 PAK をコピー中（再エンコードなし）…",
        L"操作に失敗しました。", L"入力ファイルを読み込めません。", L"出力を書き込めません。空き容量と権限を確認してください。",
        L"PAK またはマニフェストが破損しているか、非対応の形式です。", L"アーカイブに安全でないパスが含まれています。", L"このファイル名または文字コードは使用できません。",
        L"形式またはサイズの上限を超えています。", L"出力先が既に存在するため、上書きしませんでした。", L"入力フォルダーの外に有効な出力先を指定してください。",
        L"シンボリックリンクとジャンクションには対応していません。", L"操作中に入力が変更されました。再試行してください。", L"表示言語を変更しましたが、設定を保存できませんでした。",
        L"元の PAK データを保存（追加容量が必要。未変更のファイルを再利用）",
        L"元のデータを検証中…", L"元の PAK データをコピー中…", L"元の PAK をバイト単位で復元しました",
    }},
    Catalog{{
        L"WA2 PAK 도구", L"PAK 만들기 / 풀기",
        L"폴더를 선택하여 PAK을 만들거나 PAK을 새 폴더에 풉니다.",
        L"언어 / Language", L"폴더 선택 → PAK 만들기", L"PAK 선택 → 풀기",
        L"파일 이름 인코딩", L"자동 (새 파일: Shift-JIS)", L"일본어 Shift-JIS / CP932", L"중국어 GBK / CP936",
        L"LZSS 압축 (느림; 크기가 줄어드는 경우에만 압축)",
        L"다시 만들려면 .wa2pak-manifest와 .wa2pak-cache를 보관하세요.\r\n이름이 깨지면 인코딩을 바꾸고 다시 푸세요. 덮어쓰지 않습니다.",
        L"준비됨", L"취소", L"준비 중…", L"취소 중…", L"취소되었습니다. 불완전한 출력은 생성하지 않았습니다.",
        L"PAK으로 만들 폴더 선택", L"출력 위치 선택 (새 하위 폴더가 생성됩니다)", L"풀 PAK 선택", L"PAK 저장 (새 파일 이름 지정)", L"모든 파일",
        L"대상이 이미 있습니다", L"기존 파일을 보존하려면 새 PAK 파일 이름을 지정하세요.",
        L"PAK 만들기 완료", L"풀기 완료", L"파일 수",
        L"PAK 인덱스 확인 중…", L"원본 폴더 검색 중…", L"만드는 중", L"푸는 중", L"동영상 PAK 복사 중 (변환 없음)…",
        L"작업에 실패했습니다.", L"입력 파일을 읽을 수 없습니다.", L"출력을 쓸 수 없습니다. 여유 공간과 권한을 확인하세요.",
        L"PAK 또는 목록 파일이 손상되었거나 지원하지 않는 형식입니다.", L"안전하지 않은 경로가 포함되어 있습니다.", L"지원하지 않는 파일 이름 또는 인코딩입니다.",
        L"형식 또는 크기 제한을 초과했습니다.", L"대상이 이미 있어 덮어쓰지 않았습니다.", L"원본 폴더 밖의 올바른 출력 위치를 선택하세요.",
        L"심볼릭 링크 및 디렉터리 연결은 지원하지 않습니다.", L"작업 중 원본이 변경되었습니다. 다시 시도하세요.", L"이번 세션의 언어는 변경했지만 설정을 저장하지 못했습니다.",
        L"원본 PAK 데이터 보관 (추가 공간 필요; 변경되지 않은 파일 재사용)",
        L"원본 데이터 확인 중…", L"원본 PAK 데이터 복사 중…", L"원본 PAK을 바이트 단위로 복원했습니다",
    }},
    Catalog{{
        L"Інструмент WA2 PAK", L"Пакування / розпакування PAK",
        L"Виберіть папку для створення PAK або PAK для розпакування в нову папку.",
        L"Мова / Language", L"Вибрати папку → Створити PAK", L"Вибрати PAK → Розпакувати",
        L"Кодування назв", L"Авто (нові: Shift-JIS)", L"Японська Shift-JIS / CP932", L"Китайська GBK / CP936",
        L"Стиснення LZSS (повільніше; лише якщо розмір зменшується)",
        L"Збережіть .wa2pak-manifest та .wa2pak-cache для пакування.\r\nХибні назви? Змініть кодування й розпакуйте знову. Без перезапису.",
        L"Готово", L"Скасувати", L"Підготовка…", L"Скасування…", L"Скасовано. Незавершені вихідні файли не створено.",
        L"Виберіть папку для пакування", L"Виберіть місце збереження (буде створено нову підпапку)", L"Виберіть PAK для розпакування", L"Зберегти PAK (укажіть нову назву)", L"Усі файли",
        L"Місце призначення вже існує", L"Укажіть нову назву PAK, щоб зберегти наявний файл.",
        L"Пакування завершено", L"Розпакування завершено", L"Файлів",
        L"Перевірка індексу PAK…", L"Сканування вихідної папки…", L"Пакування", L"Розпакування", L"Копіювання відео PAK (без перекодування)…",
        L"Операція не вдалася.", L"Не вдалося прочитати вхідні дані.", L"Не вдалося записати результат. Перевірте вільне місце та дозволи.",
        L"PAK або маніфест пошкоджений чи має непідтримуваний формат.", L"Архів містить небезпечний шлях.", L"Назва файлу або її кодування не підтримується.",
        L"Перевищено обмеження формату або розміру.", L"Місце призначення вже існує. Його не перезаписано.", L"Виберіть коректне місце збереження поза вихідною папкою.",
        L"Символічні посилання та з'єднання каталогів не підтримуються.", L"Джерело змінилося під час операції. Спробуйте ще раз.", L"Мову змінено для цього сеансу, але налаштування не вдалося зберегти.",
        L"Зберігати дані PAK (потрібне місце; повторне використання незмінених файлів)",
        L"Перевірка оригінальних даних…", L"Копіювання оригінальних даних PAK…", L"Оригінальний PAK відновлено байт у байт",
    }},
    Catalog{{
        L"Инструмент WA2 PAK", L"Упаковка / распаковка PAK",
        L"Выберите папку для создания PAK или PAK для распаковки в новую папку.",
        L"Язык / Language", L"Выбрать папку → Создать PAK", L"Выбрать PAK → Распаковать",
        L"Кодировка имён", L"Авто (новые: Shift-JIS)", L"Японская Shift-JIS / CP932", L"Китайская GBK / CP936",
        L"Сжатие LZSS (медленнее; только если размер уменьшается)",
        L"Сохраните .wa2pak-manifest и .wa2pak-cache для упаковки.\r\nНеверные имена? Смените кодировку и распакуйте заново. Без перезаписи.",
        L"Готово", L"Отмена", L"Подготовка…", L"Отмена…", L"Отменено. Незавершённые выходные файлы не созданы.",
        L"Выберите папку для упаковки", L"Выберите место сохранения (будет создана новая подпапка)", L"Выберите PAK для распаковки", L"Сохранить PAK (укажите новое имя)", L"Все файлы",
        L"Путь назначения уже существует", L"Укажите новое имя PAK, чтобы сохранить существующий файл.",
        L"Упаковка завершена", L"Распаковка завершена", L"Файлов",
        L"Проверка индекса PAK…", L"Сканирование исходной папки…", L"Упаковка", L"Распаковка", L"Копирование видео PAK (без перекодирования)…",
        L"Операция не выполнена.", L"Не удалось прочитать входные данные.", L"Не удалось записать результат. Проверьте место на диске и права доступа.",
        L"PAK или манифест повреждён либо имеет неподдерживаемый формат.", L"Архив содержит небезопасный путь.", L"Имя файла или его кодировка не поддерживается.",
        L"Превышено ограничение формата или размера.", L"Путь назначения уже существует. Он не был перезаписан.", L"Выберите корректное место сохранения вне исходной папки.",
        L"Символические ссылки и соединения каталогов не поддерживаются.", L"Источник изменился во время операции. Повторите попытку.", L"Язык изменён для этого сеанса, но настройку не удалось сохранить.",
        L"Хранить данные PAK (нужно место; повторное использование неизменённых файлов)",
        L"Проверка исходных данных…", L"Копирование исходных данных PAK…", L"Исходный PAK восстановлен побайтно",
    }},
    Catalog{{
        L"Outil PAK WA2", L"Créer / extraire un PAK",
        L"Choisissez un dossier pour créer un PAK, ou un PAK à extraire dans un nouveau dossier.",
        L"Langue / Language", L"Choisir un dossier → Créer PAK", L"Choisir un PAK → Extraire",
        L"Encodage des noms", L"Auto (nouveau : Shift-JIS)", L"Japonais Shift-JIS / CP932", L"Chinois GBK / CP936",
        L"Compression LZSS (plus lente ; seulement si le résultat est plus petit)",
        L"Gardez .wa2pak-manifest et .wa2pak-cache pour recréer le PAK.\r\nNoms illisibles ? Changez l'encodage et réessayez. Aucun écrasement.",
        L"Prêt", L"Annuler", L"Préparation…", L"Annulation…", L"Annulé. Aucun fichier de sortie incomplet n'a été créé.",
        L"Choisir le dossier à empaqueter", L"Choisir la destination (un nouveau sous-dossier sera créé)", L"Choisir un PAK à extraire", L"Enregistrer le PAK (choisir un nouveau nom)", L"Tous les fichiers",
        L"La destination existe déjà", L"Choisissez un nouveau nom de PAK pour conserver le fichier existant.",
        L"Création terminée", L"Extraction terminée", L"Fichiers",
        L"Vérification de l'index PAK…", L"Analyse du dossier source…", L"Création", L"Extraction", L"Copie du PAK vidéo (sans transcodage)…",
        L"L'opération a échoué.", L"Impossible de lire les données d'entrée.", L"Impossible d'écrire le résultat. Vérifiez l'espace libre et les permissions.",
        L"Le PAK ou son manifeste est endommagé ou non pris en charge.", L"L'archive contient un chemin non sécurisé.", L"Le nom de fichier ou son encodage n'est pas pris en charge.",
        L"Une limite de format ou de taille est dépassée.", L"La destination existe déjà. Elle n'a pas été écrasée.", L"Choisissez une destination valide hors du dossier source.",
        L"Les liens symboliques et jonctions de dossiers ne sont pas pris en charge.", L"La source a changé pendant l'opération. Réessayez.", L"La langue a été changée pour cette session, mais le réglage n'a pas pu être enregistré.",
        L"Conserver les données PAK (espace supplémentaire ; réutiliser les fichiers inchangés)",
        L"Vérification des données d'origine…", L"Copie des données PAK d'origine…", L"PAK d'origine restauré à l'identique",
    }},
    Catalog{{
        L"WA2 PAK-Werkzeug", L"PAK erstellen / entpacken",
        L"Wählen Sie einen Ordner zum Erstellen einer PAK oder eine PAK zum Entpacken in einen neuen Ordner.",
        L"Sprache / Language", L"Ordner wählen → PAK erstellen", L"PAK wählen → Entpacken",
        L"Namenskodierung", L"Auto (neu: Shift-JIS)", L"Japanisch Shift-JIS / CP932", L"Chinesisch GBK / CP936",
        L"LZSS-Komprimierung (langsamer; nur wenn das Ergebnis kleiner ist)",
        L".wa2pak-manifest und .wa2pak-cache zum Packen aufbewahren.\r\nFalsche Namen? Kodierung ändern und neu entpacken. Kein Überschreiben.",
        L"Bereit", L"Abbrechen", L"Vorbereitung…", L"Abbruch…", L"Abgebrochen. Es wurde keine unvollständige Ausgabe erstellt.",
        L"Ordner zum Packen wählen", L"Ziel wählen (ein neuer Unterordner wird erstellt)", L"PAK zum Entpacken wählen", L"PAK speichern (neuen Dateinamen wählen)", L"Alle Dateien",
        L"Das Ziel ist bereits vorhanden", L"Wählen Sie einen neuen PAK-Dateinamen, um die vorhandene Datei zu erhalten.",
        L"Packen abgeschlossen", L"Entpacken abgeschlossen", L"Dateien",
        L"PAK-Index wird geprüft…", L"Quellordner wird durchsucht…", L"Packen", L"Entpacken", L"Video-PAK wird kopiert (ohne Umkodierung)…",
        L"Der Vorgang ist fehlgeschlagen.", L"Die Eingabe konnte nicht gelesen werden.", L"Die Ausgabe konnte nicht geschrieben werden. Speicherplatz und Rechte prüfen.",
        L"Die PAK oder ihr Manifest ist beschädigt oder wird nicht unterstützt.", L"Das Archiv enthält einen unsicheren Pfad.", L"Dateiname oder Namenskodierung wird nicht unterstützt.",
        L"Eine Format- oder Größenbegrenzung wurde überschritten.", L"Das Ziel ist bereits vorhanden und wurde nicht überschrieben.", L"Wählen Sie ein gültiges Ziel außerhalb des Quellordners.",
        L"Symbolische Links und Verzeichnisverknüpfungen werden nicht unterstützt.", L"Die Quelle wurde während des Vorgangs geändert. Erneut versuchen.", L"Die Sprache wurde für diese Sitzung geändert, konnte aber nicht gespeichert werden.",
        L"PAK-Daten erhalten (zusätzlicher Speicher; unveränderte Dateien wiederverwenden)",
        L"Originaldaten werden geprüft…", L"Originale PAK-Daten werden kopiert…", L"Original-PAK bytegenau wiederhergestellt",
    }},
    Catalog{{
        L"Herramienta PAK WA2", L"Crear / extraer PAK",
        L"Elija una carpeta para crear un PAK o un PAK para extraerlo en una carpeta nueva.",
        L"Idioma / Language", L"Elegir carpeta → Crear PAK", L"Elegir PAK → Extraer",
        L"Codificación", L"Auto (nuevo: Shift-JIS)", L"Japonés Shift-JIS / CP932", L"Chino GBK / CP936",
        L"Compresión LZSS (más lenta; solo si reduce el tamaño)",
        L"Conserve .wa2pak-manifest y .wa2pak-cache para crear el PAK.\r\n¿Nombres incorrectos? Cambie la codificación y extraiga de nuevo. Sin sobrescribir.",
        L"Listo", L"Cancelar", L"Preparando…", L"Cancelando…", L"Cancelado. No se ha creado ningún archivo de salida incompleto.",
        L"Elegir la carpeta que se empaquetará", L"Elegir destino (se creará una subcarpeta nueva)", L"Elegir un PAK para extraer", L"Guardar PAK (elija un nombre nuevo)", L"Todos los archivos",
        L"El destino ya existe", L"Elija un nombre de PAK nuevo para conservar el archivo existente.",
        L"Creación completada", L"Extracción completada", L"Archivos",
        L"Comprobando el índice PAK…", L"Examinando la carpeta de origen…", L"Empaquetando", L"Extrayendo", L"Copiando el PAK de vídeo (sin conversión)…",
        L"La operación ha fallado.", L"No se pudo leer la entrada.", L"No se pudo escribir la salida. Revise el espacio libre y los permisos.",
        L"El PAK o su manifiesto está dañado o no es compatible.", L"El archivo contiene una ruta insegura.", L"El nombre o su codificación no es compatible.",
        L"Se ha superado un límite de formato o tamaño.", L"El destino ya existe y no se ha sobrescrito.", L"Elija un destino válido fuera de la carpeta de origen.",
        L"No se admiten enlaces simbólicos ni uniones de directorios.", L"El origen cambió durante la operación. Inténtelo de nuevo.", L"El idioma se ha cambiado para esta sesión, pero no se pudo guardar la preferencia.",
        L"Conservar datos PAK (espacio adicional; reutilizar archivos sin cambios)",
        L"Comprobando los datos originales…", L"Copiando los datos PAK originales…", L"PAK original restaurado byte por byte",
    }},
    Catalog{{
        L"Ferramenta PAK WA2", L"Criar / extrair PAK",
        L"Escolha uma pasta para criar um PAK ou um PAK para extrair para uma nova pasta.",
        L"Idioma / Language", L"Escolher pasta → Criar PAK", L"Escolher PAK → Extrair",
        L"Codificação", L"Auto (novo: Shift-JIS)", L"Japonês Shift-JIS / CP932", L"Chinês GBK / CP936",
        L"Compressão LZSS (mais lenta; apenas se reduzir o tamanho)",
        L"Guarde .wa2pak-manifest e .wa2pak-cache para criar o PAK.\r\nNomes incorretos? Mude a codificação e extraia de novo. Sem substituir.",
        L"Pronto", L"Cancelar", L"A preparar…", L"A cancelar…", L"Cancelado. Não foi criado nenhum ficheiro de saída incompleto.",
        L"Escolher a pasta a empacotar", L"Escolher o destino (será criada uma nova subpasta)", L"Escolher um PAK para extrair", L"Guardar PAK (escolha um novo nome)", L"Todos os ficheiros",
        L"O destino já existe", L"Escolha um novo nome para o PAK para manter o ficheiro existente.",
        L"Criação concluída", L"Extração concluída", L"Ficheiros",
        L"A verificar o índice PAK…", L"A analisar a pasta de origem…", L"A empacotar", L"A extrair", L"A copiar o PAK de vídeo (sem conversão)…",
        L"A operação falhou.", L"Não foi possível ler a entrada.", L"Não foi possível escrever a saída. Verifique o espaço livre e as permissões.",
        L"O PAK ou o manifesto está danificado ou não é compatível.", L"O arquivo contém um caminho inseguro.", L"O nome do ficheiro ou a codificação não é compatível.",
        L"Foi ultrapassado um limite de formato ou tamanho.", L"O destino já existe e não foi substituído.", L"Escolha um destino válido fora da pasta de origem.",
        L"Não são suportadas ligações simbólicas nem junções de diretórios.", L"A origem mudou durante a operação. Tente novamente.", L"O idioma foi alterado para esta sessão, mas não foi possível guardar a preferência.",
        L"Conservar dados PAK (espaço adicional; reutilizar ficheiros sem alterações)",
        L"A verificar os dados originais…", L"A copiar os dados PAK originais…", L"PAK original restaurado byte a byte",
    }},
}};
constexpr bool complete_catalogs() {
    for(const auto& catalog : catalogs) for(const auto text : catalog) if(text.empty()) return false;
    return true;
}
static_assert(complete_catalogs(), "Every supported language needs every UI string");
std::size_t index(Language language) noexcept {
    const auto value = std::size_t(language);
    return value < languages.size() ? value : std::size_t(default_language);
}
}
Language parse_language(std::string_view code) noexcept {
    for(const auto& language : languages) if(language.code == code) return language.id;
    return default_language;
}
std::string_view language_code(Language language) noexcept { return languages[index(language)].code; }
std::wstring_view tr(Language language, Text text) noexcept {
    const auto key = std::size_t(text);
    return catalogs[index(language)][key < std::size_t(Text::count) ? key : std::size_t(Text::error_general)];
}
std::wstring progress_text(Language language, ProgressStage stage, std::wstring_view detail) {
    Text text;
    switch(stage) {
    case ProgressStage::checking_index: text = Text::checking_index; break;
    case ProgressStage::scanning_folder: text = Text::scanning_folder; break;
    case ProgressStage::packing_file: text = Text::packing_file; break;
    case ProgressStage::unpacking_file: text = Text::unpacking_file; break;
    case ProgressStage::copying_movie: text = Text::copying_movie; break;
    case ProgressStage::packed: text = Text::packed; break;
    case ProgressStage::unpacked: text = Text::unpacked; break;
    case ProgressStage::checking_original: text = Text::checking_original; break;
    case ProgressStage::copying_original: text = Text::copying_original; break;
    default: throw Error("Unknown progress stage");
    }
    std::wstring result(tr(language, text));
    if(!detail.empty()) { result += L": "; result += detail; }
    return result;
}
std::wstring error_text(Language language, ErrorKind kind, std::wstring_view detail) {
    Text text = Text::error_general;
    switch(kind) {
    case ErrorKind::read: text = Text::error_read; break;
    case ErrorKind::write: text = Text::error_write; break;
    case ErrorKind::format: text = Text::error_format; break;
    case ErrorKind::unsafe_path: text = Text::error_unsafe; break;
    case ErrorKind::filename: text = Text::error_filename; break;
    case ErrorKind::size_limit: text = Text::error_limit; break;
    case ErrorKind::target_exists: text = Text::error_exists; break;
    case ErrorKind::invalid_target: text = Text::error_target; break;
    case ErrorKind::link: text = Text::error_link; break;
    case ErrorKind::source_changed: text = Text::error_changed; break;
    case ErrorKind::cancelled: text = Text::cancelled; break;
    default: break;
    }
    if(language == Language::english && !detail.empty()) return std::wstring(detail);
    std::wstring result(tr(language, text));
    if(!detail.empty()) { result += L"\r\n"; result += detail; }
    return result;
}
}
