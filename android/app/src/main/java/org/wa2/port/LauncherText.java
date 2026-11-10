package org.wa2.port;

import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** Launcher presentation only. Never changes the process or game locale. */
final class LauncherText {
    enum Key {
        UI_LANGUAGE, LOGS, COPY_LOG, FOLDER_NONE, FOLDER_FMT, PICK_FOLDER, GAME_FMT, PICK_GAME, SUBTITLE_FMT, BUILT_IN,
        EXTERNAL, SUBTITLE_SOURCE, CHOOSE_SUBTITLE, CAPTIONS, FPS, FPS_DETAIL, LONG_PRESS, TEST_MODE, START, CANCEL,
        SAVING, SAVED, SAVE_ERROR, NEED_FOLDER, LOCATION_HINT, CHECKING_RESOURCES, READY, STARTING, CHECKING_SUBTITLE, SUBTITLE_OK,
        SUBTITLE_REJECTED, CANCELLED_SUBTITLE, CANCELLED_FOLDER, OPERATION_FAILED, PICKER_FAILED, NEED_REAUTHORIZE, REQUIRE_LOCAL_SUBTITLE, READING_LOGS, COPIED_LOGS, LOG_MISSING, LOG_FAILED, SHOW_MENU, SCRIPT_PATH, FONT_PATH
    }
    private static final class Entry {
        final String tag,name;final boolean rtl;final String[] words;
        Entry(String tag,String name,boolean rtl,String words){this.tag=tag;this.name=name;this.rtl=rtl;this.words=words.split("\\|",-1);}
    }
    private static Entry entry(String tag,String name,boolean rtl,String menuLabel,String scriptPath,String fontPath,String words){return new Entry(tag,name,rtl,words+"|"+menuLabel+"|"+scriptPath+"|"+fontPath);}
    private static final Entry[] ENTRIES={
        entry("zh-CN","简体中文",false,"显示右上角菜单","脚本包（游戏目录内）：%s","字库包（游戏目录内）：%s",
            "界面语言|诊断|复制上次运行诊断|未选择游戏目录|游戏目录：%s|选择目录|游戏：%s|选择游戏语言|字幕：%s|内置|"+
            "外置|字幕来源|选择 sub.yaml|补充字幕|显示 FPS|显示 FPS 和帧耗时|长按快进|测试模式（解锁全部）|开始游戏|取消|"+
            "正在保存…|已保存，下次启动生效。|保存失败：%s|请先选择游戏目录。|设置和存档保存在所选目录。|正在检查资源…|已找到 %d 个资源包。|正在启动…|正在检查字幕…|字幕已启用：%s|"+
            "字幕未启用：%s|已取消，保留原字幕。|已取消，保留原目录。|操作失败：%s|无法打开系统文件选择器。|请重新授权可读写的游戏目录。|请选择可持久读取的本地 sub.yaml。|正在读取诊断…|已复制诊断。|暂无诊断，请正常退出游戏后重试。|诊断失败：%s"),
        entry("zh-TW","繁體中文",false,"顯示右上角選單","腳本包（遊戲目錄內）：%s","字庫包（遊戲目錄內）：%s",
            "介面語言|診斷|複製上次執行診斷|尚未選擇遊戲目錄|遊戲目錄：%s|選擇目錄|遊戲：%s|選擇遊戲語言|字幕：%s|內建|"+
            "外部|字幕來源|選擇 sub.yaml|補充字幕|顯示 FPS|顯示 FPS 與影格耗時|長按快轉|測試模式（全部解鎖）|開始遊戲|取消|"+
            "儲存中…|已儲存，下次啟動生效。|儲存失敗：%s|請先選擇遊戲目錄。|設定與存檔儲存在所選目錄。|正在檢查資源…|找到 %d 個資源包。|啟動中…|正在檢查字幕…|字幕已啟用：%s|"+
            "字幕未啟用：%s|已取消，保留原字幕。|已取消，保留原目錄。|操作失敗：%s|無法開啟系統檔案選擇器。|請重新授權可讀寫的遊戲目錄。|請選擇可持續讀取的本機 sub.yaml。|正在讀取診斷…|已複製診斷。|暫無診斷，請正常離開遊戲後重試。|診斷失敗：%s"),
        entry("en","English",false,"Show top-right menu","Script PAK (game folder): %s","Font PAK (game folder): %s",
            "Interface language|Logs|Copy last run diagnostics|No game folder selected|Game folder: %s|Choose folder|Game: %s|Game language|Captions: %s|Built-in|"+
            "External|Caption source|Choose sub.yaml|Extra captions|Show FPS|Show FPS and frame time|Hold to skip|Test mode (unlock all)|Start game|Cancel|"+
            "Saving…|Saved for next launch.|Could not save: %s|Choose a game folder first.|Settings and saves use the selected folder.|Checking resources…|Found %d archives.|Starting…|Checking captions…|Captions enabled: %s|"+
            "Captions not enabled: %s|Cancelled; captions unchanged.|Cancelled; folder unchanged.|Operation failed: %s|Cannot open the system file picker.|Grant read/write access to the game folder again.|Choose a local sub.yaml with persistent read access.|Reading diagnostics…|Diagnostics copied.|No diagnostics yet. Exit the game normally and retry.|Diagnostics failed: %s"),
        entry("ja","日本語",false,"右上のメニューを表示","シナリオPAK（ゲームフォルダー内）：%s","フォントPAK（ゲームフォルダー内）：%s",
            "表示言語|診断|前回の診断をコピー|ゲームフォルダー未選択|ゲームフォルダー：%s|フォルダー選択|ゲーム：%s|ゲームの言語|字幕：%s|内蔵|"+
            "外部|字幕の読み込み元|sub.yaml を選択|補足字幕|FPS を表示|FPS とフレーム時間を表示|長押しで早送り|テストモード（全解放）|ゲーム開始|キャンセル|"+
            "保存中…|保存しました。次回起動時に適用します。|保存できません：%s|ゲームフォルダーを選択してください。|設定とセーブは選択したフォルダーに保存します。|リソースを確認中…|%d 個のアーカイブが見つかりました。|起動中…|字幕を確認中…|字幕を有効にしました：%s|"+
            "字幕は有効になりませんでした：%s|キャンセルしました。字幕は変更しません。|キャンセルしました。フォルダーは変更しません。|操作に失敗しました：%s|システムのファイル選択画面を開けません。|ゲームフォルダーへの読み書きを再度許可してください。|継続して読み取れるローカルの sub.yaml を選択してください。|診断を読み込み中…|診断をコピーしました。|診断がありません。ゲームを正常終了して再試行してください。|診断に失敗しました：%s"),
        entry("ko","한국어",false,"오른쪽 위 메뉴 표시","스크립트 PAK (게임 폴더): %s","글꼴 PAK (게임 폴더): %s",
            "화면 언어|진단|최근 실행 진단 복사|게임 폴더 미선택|게임 폴더: %s|폴더 선택|게임: %s|게임 언어|자막: %s|내장|"+
            "외부|자막 소스|sub.yaml 선택|추가 자막|FPS 표시|FPS 및 프레임 시간 표시|길게 눌러 빨리 감기|테스트 모드 (모두 해제)|게임 시작|취소|"+
            "저장 중…|저장됨. 다음 실행 시 적용됩니다.|저장 실패: %s|먼저 게임 폴더를 선택하세요.|설정과 저장 데이터는 선택한 폴더에 보관됩니다.|리소스 확인 중…|리소스 파일 %d개를 찾았습니다.|시작 중…|자막 확인 중…|자막 활성화: %s|"+
            "자막을 활성화하지 못함: %s|취소됨. 기존 자막을 유지합니다.|취소됨. 기존 폴더를 유지합니다.|작업 실패: %s|시스템 파일 선택기를 열 수 없습니다.|게임 폴더의 읽기/쓰기 권한을 다시 허용하세요.|지속적으로 읽을 수 있는 로컬 sub.yaml을 선택하세요.|진단 읽는 중…|진단을 복사했습니다.|진단이 없습니다. 게임을 정상 종료한 뒤 다시 시도하세요.|진단 실패: %s"),
        entry("fr","Français",false,"Afficher le menu en haut à droite","PAK du script (dossier du jeu) : %s","PAK de police (dossier du jeu) : %s",
            "Langue de l’interface|Journal|Copier le dernier diagnostic|Aucun dossier de jeu|Dossier du jeu : %s|Choisir dossier|Jeu : %s|Langue du jeu|Sous-titres : %s|Intégrés|"+
            "Externes|Source des sous-titres|Choisir sub.yaml|Sous-titres supplémentaires|Afficher FPS|Afficher les FPS et le temps par image|Maintenir pour avancer|Mode test (tout débloquer)|Jouer|Annuler|"+
            "Enregistrement…|Enregistré pour le prochain lancement.|Échec de l’enregistrement : %s|Choisissez d’abord le dossier du jeu.|Paramètres et sauvegardes dans le dossier choisi.|Vérification des ressources…|%d archives trouvées.|Démarrage…|Vérification des sous-titres…|Sous-titres activés : %s|"+
            "Sous-titres non activés : %s|Annulé ; sous-titres conservés.|Annulé ; dossier conservé.|Échec de l’opération : %s|Impossible d’ouvrir le sélecteur de fichiers.|Autorisez à nouveau la lecture et l’écriture du dossier du jeu.|Choisissez un sub.yaml local avec accès en lecture persistant.|Lecture du diagnostic…|Diagnostic copié.|Aucun diagnostic. Quittez normalement le jeu puis réessayez.|Échec du diagnostic : %s"),
        entry("de","Deutsch",false,"Menü oben rechts anzeigen","Skript-PAK (Spielordner): %s","Schrift-PAK (Spielordner): %s",
            "Sprache der Oberfläche|Protokoll|Letzte Diagnose kopieren|Kein Spielordner gewählt|Spielordner: %s|Ordner wählen|Spiel: %s|Spielsprache|Untertitel: %s|Intern|"+
            "Extern|Untertitelquelle|sub.yaml wählen|Zusätzliche Untertitel|FPS anzeigen|FPS und Bildzeit anzeigen|Halten zum Vorspulen|Testmodus (alles freischalten)|Spiel starten|Abbrechen|"+
            "Speichern…|Für den nächsten Start gespeichert.|Speichern fehlgeschlagen: %s|Zuerst einen Spielordner wählen.|Einstellungen und Spielstände liegen im gewählten Ordner.|Ressourcen prüfen…|%d Archive gefunden.|Starten…|Untertitel prüfen…|Untertitel aktiviert: %s|"+
            "Untertitel nicht aktiviert: %s|Abgebrochen; Untertitel unverändert.|Abgebrochen; Ordner unverändert.|Vorgang fehlgeschlagen: %s|Die Systemdateiauswahl lässt sich nicht öffnen.|Lese- und Schreibzugriff auf den Spielordner erneut erlauben.|Eine lokale sub.yaml mit dauerhaftem Lesezugriff wählen.|Diagnose lesen…|Diagnose kopiert.|Keine Diagnose. Das Spiel regulär beenden und erneut versuchen.|Diagnose fehlgeschlagen: %s"),
        entry("es","Español",false,"Mostrar menú superior derecho","PAK de guion (carpeta del juego): %s","PAK de fuente (carpeta del juego): %s",
            "Idioma de la interfaz|Registro|Copiar último diagnóstico|Sin carpeta de juego|Carpeta del juego: %s|Elegir carpeta|Juego: %s|Idioma del juego|Subtítulos: %s|Integrados|"+
            "Externos|Origen de subtítulos|Elegir sub.yaml|Subtítulos adicionales|Mostrar FPS|Mostrar FPS y tiempo de fotograma|Mantener para avanzar|Modo de prueba (desbloquear todo)|Iniciar juego|Cancelar|"+
            "Guardando…|Guardado para el próximo inicio.|No se pudo guardar: %s|Elige primero la carpeta del juego.|Ajustes y partidas en la carpeta elegida.|Comprobando recursos…|Se encontraron %d archivos.|Iniciando…|Comprobando subtítulos…|Subtítulos activados: %s|"+
            "Subtítulos no activados: %s|Cancelado; subtítulos sin cambios.|Cancelado; carpeta sin cambios.|Error de operación: %s|No se puede abrir el selector de archivos.|Vuelve a permitir lectura y escritura en la carpeta del juego.|Elige un sub.yaml local con permiso de lectura permanente.|Leyendo diagnóstico…|Diagnóstico copiado.|No hay diagnóstico. Sal normalmente del juego e inténtalo de nuevo.|Error de diagnóstico: %s"),
        entry("pt-BR","Português (Brasil)",false,"Mostrar menu no canto superior direito","PAK de roteiro (pasta do jogo): %s","PAK de fonte (pasta do jogo): %s",
            "Idioma da interface|Registro|Copiar último diagnóstico|Nenhuma pasta de jogo|Pasta do jogo: %s|Escolher pasta|Jogo: %s|Idioma do jogo|Legendas: %s|Internas|"+
            "Externas|Origem das legendas|Escolher sub.yaml|Legendas adicionais|Mostrar FPS|Mostrar FPS e tempo por quadro|Segure para avançar|Modo de teste (liberar tudo)|Iniciar jogo|Cancelar|"+
            "Salvando…|Salvo para a próxima inicialização.|Falha ao salvar: %s|Escolha primeiro a pasta do jogo.|Configurações e saves ficam na pasta escolhida.|Verificando recursos…|%d arquivos encontrados.|Iniciando…|Verificando legendas…|Legendas ativadas: %s|"+
            "Legendas não ativadas: %s|Cancelado; legendas mantidas.|Cancelado; pasta mantida.|Falha na operação: %s|Não foi possível abrir o seletor de arquivos.|Permita novamente a leitura e escrita na pasta do jogo.|Escolha um sub.yaml local com acesso de leitura permanente.|Lendo diagnóstico…|Diagnóstico copiado.|Sem diagnóstico. Saia normalmente do jogo e tente de novo.|Falha no diagnóstico: %s"),
        entry("it","Italiano",false,"Mostra menu in alto a destra","PAK script (cartella del gioco): %s","PAK font (cartella del gioco): %s",
            "Lingua dell’interfaccia|Registro|Copia ultima diagnostica|Nessuna cartella di gioco|Cartella del gioco: %s|Scegli cartella|Gioco: %s|Lingua del gioco|Sottotitoli: %s|Integrati|"+
            "Esterni|Origine dei sottotitoli|Scegli sub.yaml|Sottotitoli aggiuntivi|Mostra FPS|Mostra FPS e tempo per fotogramma|Tieni premuto per avanzare|Modalità test (sblocca tutto)|Avvia gioco|Annulla|"+
            "Salvataggio…|Salvato per il prossimo avvio.|Salvataggio non riuscito: %s|Scegli prima la cartella del gioco.|Impostazioni e salvataggi nella cartella scelta.|Verifica risorse…|Trovati %d archivi.|Avvio…|Verifica sottotitoli…|Sottotitoli attivati: %s|"+
            "Sottotitoli non attivati: %s|Annullato; sottotitoli invariati.|Annullato; cartella invariata.|Operazione non riuscita: %s|Impossibile aprire il selettore di file.|Autorizza di nuovo lettura e scrittura nella cartella del gioco.|Scegli un sub.yaml locale con accesso in lettura permanente.|Lettura diagnostica…|Diagnostica copiata.|Nessuna diagnostica. Esci normalmente dal gioco e riprova.|Diagnostica non riuscita: %s"),
        entry("ru","Русский",false,"Показывать меню в правом верхнем углу","PAK сценариев (папка игры): %s","PAK шрифтов (папка игры): %s",
            "Язык интерфейса|Журнал|Скопировать последнюю диагностику|Папка игры не выбрана|Папка игры: %s|Выбрать папку|Игра: %s|Язык игры|Субтитры: %s|Встроенные|"+
            "Внешние|Источник субтитров|Выбрать sub.yaml|Дополнительные субтитры|Показывать FPS|Показывать FPS и время кадра|Удерживать для перемотки|Тестовый режим (открыть всё)|Начать игру|Отмена|"+
            "Сохранение…|Сохранено для следующего запуска.|Не удалось сохранить: %s|Сначала выберите папку игры.|Настройки и сохранения находятся в выбранной папке.|Проверка ресурсов…|Найдено архивов: %d.|Запуск…|Проверка субтитров…|Субтитры включены: %s|"+
            "Субтитры не включены: %s|Отменено; субтитры не изменены.|Отменено; папка не изменена.|Ошибка операции: %s|Не удалось открыть выбор файлов.|Снова разрешите чтение и запись в папку игры.|Выберите локальный sub.yaml с постоянным доступом на чтение.|Чтение диагностики…|Диагностика скопирована.|Диагностики пока нет. Завершите игру обычным способом и повторите.|Ошибка диагностики: %s"),
        entry("uk","Українська",false,"Показувати меню вгорі праворуч","PAK сценаріїв (тека гри): %s","PAK шрифтів (тека гри): %s",
            "Мова інтерфейсу|Журнал|Копіювати останню діагностику|Теку гри не вибрано|Тека гри: %s|Вибрати теку|Гра: %s|Мова гри|Субтитри: %s|Вбудовані|"+
            "Зовнішні|Джерело субтитрів|Вибрати sub.yaml|Додаткові субтитри|Показувати FPS|Показувати FPS і час кадру|Утримувати для перемотування|Тестовий режим (відкрити все)|Почати гру|Скасувати|"+
            "Збереження…|Збережено для наступного запуску.|Не вдалося зберегти: %s|Спочатку виберіть теку гри.|Налаштування та збереження у вибраній теці.|Перевірка ресурсів…|Знайдено архівів: %d.|Запуск…|Перевірка субтитрів…|Субтитри ввімкнено: %s|"+
            "Субтитри не ввімкнено: %s|Скасовано; субтитри не змінено.|Скасовано; теку не змінено.|Помилка операції: %s|Не вдалося відкрити вибір файлів.|Знову дозвольте читання й запис у теку гри.|Виберіть локальний sub.yaml із постійним доступом для читання.|Читання діагностики…|Діагностику скопійовано.|Діагностики ще немає. Вийдіть із гри звичайним способом і повторіть.|Помилка діагностики: %s"),
        entry("pl","Polski",false,"Pokaż menu w prawym górnym rogu","PAK skryptu (folder gry): %s","PAK czcionki (folder gry): %s",
            "Język interfejsu|Dziennik|Kopiuj ostatnią diagnostykę|Nie wybrano folderu gry|Folder gry: %s|Wybierz folder|Gra: %s|Język gry|Napisy: %s|Wbudowane|"+
            "Zewnętrzne|Źródło napisów|Wybierz sub.yaml|Dodatkowe napisy|Pokaż FPS|Pokaż FPS i czas klatki|Przytrzymaj, aby przewijać|Tryb testowy (odblokuj wszystko)|Uruchom grę|Anuluj|"+
            "Zapisywanie…|Zapisano na następne uruchomienie.|Nie można zapisać: %s|Najpierw wybierz folder gry.|Ustawienia i zapisy są w wybranym folderze.|Sprawdzanie zasobów…|Znaleziono archiwa: %d.|Uruchamianie…|Sprawdzanie napisów…|Napisy włączone: %s|"+
            "Nie włączono napisów: %s|Anulowano; napisy bez zmian.|Anulowano; folder bez zmian.|Operacja nie powiodła się: %s|Nie można otworzyć systemowego wyboru plików.|Ponownie zezwól na odczyt i zapis w folderze gry.|Wybierz lokalny sub.yaml ze stałym dostępem do odczytu.|Odczytywanie diagnostyki…|Diagnostyka skopiowana.|Brak diagnostyki. Wyjdź z gry normalnie i spróbuj ponownie.|Błąd diagnostyki: %s"),
        entry("tr","Türkçe",false,"Sağ üst menüyü göster","Betik PAK (oyun klasörü): %s","Yazı tipi PAK (oyun klasörü): %s",
            "Arayüz dili|Günlük|Son tanılama kaydını kopyala|Oyun klasörü seçilmedi|Oyun klasörü: %s|Klasör seç|Oyun: %s|Oyun dili|Altyazı: %s|Dahili|"+
            "Harici|Altyazı kaynağı|sub.yaml seç|Ek altyazılar|FPS göster|FPS ve kare süresini göster|Hızlı ilerlemek için basılı tut|Test modu (tümünü aç)|Oyunu başlat|İptal|"+
            "Kaydediliyor…|Sonraki açılış için kaydedildi.|Kaydedilemedi: %s|Önce oyun klasörünü seçin.|Ayarlar ve kayıtlar seçilen klasörde tutulur.|Kaynaklar denetleniyor…|%d arşiv bulundu.|Başlatılıyor…|Altyazılar denetleniyor…|Altyazılar etkin: %s|"+
            "Altyazılar etkinleştirilmedi: %s|İptal edildi; altyazılar değişmedi.|İptal edildi; klasör değişmedi.|İşlem başarısız: %s|Sistem dosya seçicisi açılamıyor.|Oyun klasörüne yeniden okuma/yazma izni verin.|Kalıcı okuma izni olan yerel bir sub.yaml seçin.|Tanılama okunuyor…|Tanılama kopyalandı.|Henüz tanılama yok. Oyundan normal çıkıp tekrar deneyin.|Tanılama başarısız: %s"),
        entry("nl","Nederlands",false,"Menu rechtsboven tonen","Script-PAK (spelmap): %s","Lettertype-PAK (spelmap): %s",
            "Interfacetaal|Logboek|Laatste diagnose kopiëren|Geen spelmap gekozen|Spelmap: %s|Map kiezen|Spel: %s|Speltaal|Ondertitels: %s|Ingebouwd|"+
            "Extern|Ondertitelbron|sub.yaml kiezen|Extra ondertitels|FPS tonen|FPS en frametijd tonen|Ingedrukt houden om door te spoelen|Testmodus (alles vrijgeven)|Spel starten|Annuleren|"+
            "Opslaan…|Opgeslagen voor de volgende start.|Opslaan mislukt: %s|Kies eerst een spelmap.|Instellingen en spelstanden staan in de gekozen map.|Bronnen controleren…|%d archieven gevonden.|Starten…|Ondertitels controleren…|Ondertitels ingeschakeld: %s|"+
            "Ondertitels niet ingeschakeld: %s|Geannuleerd; ondertitels ongewijzigd.|Geannuleerd; map ongewijzigd.|Bewerking mislukt: %s|De systeembestandskiezer kan niet worden geopend.|Geef opnieuw lees- en schrijfrechten voor de spelmap.|Kies een lokale sub.yaml met blijvende leestoegang.|Diagnose lezen…|Diagnose gekopieerd.|Nog geen diagnose. Sluit het spel normaal af en probeer opnieuw.|Diagnose mislukt: %s"),
        entry("sv","Svenska",false,"Visa menyn uppe till höger","Skript-PAK (spelmapp): %s","Typsnitts-PAK (spelmapp): %s",
            "Gränssnittsspråk|Logg|Kopiera senaste diagnostiken|Ingen spelmapp vald|Spelmapp: %s|Välj mapp|Spel: %s|Spelspråk|Undertexter: %s|Inbyggda|"+
            "Externa|Undertextkälla|Välj sub.yaml|Extra undertexter|Visa FPS|Visa FPS och bildtid|Håll för snabbspolning|Testläge (lås upp allt)|Starta spelet|Avbryt|"+
            "Sparar…|Sparat till nästa start.|Kunde inte spara: %s|Välj först en spelmapp.|Inställningar och sparfiler finns i den valda mappen.|Kontrollerar resurser…|Hittade %d arkiv.|Startar…|Kontrollerar undertexter…|Undertexter aktiverade: %s|"+
            "Undertexter inte aktiverade: %s|Avbrutet; undertexter oförändrade.|Avbrutet; mapp oförändrad.|Åtgärden misslyckades: %s|Kan inte öppna systemets filväljare.|Tillåt läsning och skrivning i spelmappen igen.|Välj en lokal sub.yaml med beständig läsåtkomst.|Läser diagnostik…|Diagnostik kopierad.|Ingen diagnostik ännu. Avsluta spelet normalt och försök igen.|Diagnostiken misslyckades: %s"),
        entry("ar","العربية",true,"إظهار القائمة أعلى اليمين","PAK النصوص (مجلد اللعبة): %s","PAK الخطوط (مجلد اللعبة): %s",
            "لغة الواجهة|السجل|نسخ آخر تقرير تشخيص|لم يتم اختيار مجلد اللعبة|مجلد اللعبة: %s|اختيار مجلد|اللعبة: %s|لغة اللعبة|الترجمة: %s|مدمجة|"+
            "خارجية|مصدر الترجمة|اختيار sub.yaml|ترجمة إضافية|عرض FPS|عرض FPS وزمن الإطار|اضغط مطولاً للتقديم|وضع الاختبار (فتح الكل)|بدء اللعبة|إلغاء|"+
            "جارٍ الحفظ…|تم الحفظ للتشغيل القادم.|تعذر الحفظ: %s|اختر مجلد اللعبة أولاً.|تُحفظ الإعدادات وملفات الحفظ في المجلد المحدد.|جارٍ فحص الموارد…|تم العثور على %d من الأرشيفات.|جارٍ البدء…|جارٍ فحص الترجمة…|تم تفعيل الترجمة: %s|"+
            "لم يتم تفعيل الترجمة: %s|أُلغي؛ لم تتغير الترجمة.|أُلغي؛ لم يتغير المجلد.|فشلت العملية: %s|تعذر فتح منتقي الملفات في النظام.|امنح مجلد اللعبة إذن القراءة والكتابة مجدداً.|اختر ملف sub.yaml محلياً مع إذن قراءة دائم.|جارٍ قراءة التشخيص…|تم نسخ التشخيص.|لا يوجد تشخيص بعد. اخرج من اللعبة بشكل طبيعي وحاول مجدداً.|فشل التشخيص: %s"),
        entry("fa","فارسی",true,"نمایش منوی بالا سمت راست","PAK متن (پوشه بازی): %s","PAK قلم (پوشه بازی): %s",
            "زبان رابط|گزارش|کپی آخرین گزارش عیب‌یابی|پوشه بازی انتخاب نشده|پوشه بازی: %s|انتخاب پوشه|بازی: %s|زبان بازی|زیرنویس: %s|داخلی|"+
            "خارجی|منبع زیرنویس|انتخاب sub.yaml|زیرنویس تکمیلی|نمایش FPS|نمایش FPS و زمان هر فریم|نگه‌داشتن برای جلو بردن|حالت آزمایش (باز کردن همه)|شروع بازی|لغو|"+
            "در حال ذخیره…|برای اجرای بعد ذخیره شد.|ذخیره نشد: %s|ابتدا پوشه بازی را انتخاب کنید.|تنظیمات و ذخیره‌ها در پوشه انتخاب‌شده قرار می‌گیرند.|بررسی منابع…|%d آرشیو پیدا شد.|در حال شروع…|بررسی زیرنویس…|زیرنویس فعال شد: %s|"+
            "زیرنویس فعال نشد: %s|لغو شد؛ زیرنویس تغییر نکرد.|لغو شد؛ پوشه تغییر نکرد.|عملیات ناموفق بود: %s|انتخابگر فایل سیستم باز نمی‌شود.|دوباره اجازه خواندن و نوشتن در پوشه بازی را بدهید.|یک sub.yaml محلی با دسترسی خواندن دائمی انتخاب کنید.|خواندن گزارش عیب‌یابی…|گزارش عیب‌یابی کپی شد.|گزارشی نیست. عادی از بازی خارج شوید و دوباره تلاش کنید.|عیب‌یابی ناموفق بود: %s"),
        entry("he","עברית",true,"הצגת התפריט בפינה הימנית העליונה","PAK תסריט (תיקיית המשחק): %s","PAK גופן (תיקיית המשחק): %s",
            "שפת הממשק|יומן|העתקת האבחון האחרון|לא נבחרה תיקיית משחק|תיקיית המשחק: %s|בחירת תיקייה|משחק: %s|שפת המשחק|כתוביות: %s|מובנות|"+
            "חיצוניות|מקור הכתוביות|בחירת sub.yaml|כתוביות נוספות|הצגת FPS|הצגת FPS וזמן פריים|לחיצה ממושכת להרצה מהירה|מצב בדיקה (פתיחת הכול)|הפעלת המשחק|ביטול|"+
            "שומר…|נשמר להפעלה הבאה.|השמירה נכשלה: %s|יש לבחור תחילה תיקיית משחק.|ההגדרות והשמירות נמצאות בתיקייה שנבחרה.|בודק משאבים…|נמצאו %d ארכיונים.|מפעיל…|בודק כתוביות…|הכתוביות הופעלו: %s|"+
            "הכתוביות לא הופעלו: %s|בוטל; הכתוביות לא השתנו.|בוטל; התיקייה לא השתנתה.|הפעולה נכשלה: %s|לא ניתן לפתוח את בורר הקבצים של המערכת.|יש לאשר שוב קריאה וכתיבה לתיקיית המשחק.|יש לבחור sub.yaml מקומי עם הרשאת קריאה קבועה.|קורא אבחון…|האבחון הועתק.|אין אבחון עדיין. יש לצאת מהמשחק כרגיל ולנסות שוב.|האבחון נכשל: %s"),
        entry("hi","हिन्दी",false,"ऊपर दाईं ओर मेन्यू दिखाएँ","स्क्रिप्ट PAK (गेम फ़ोल्डर): %s","फ़ॉन्ट PAK (गेम फ़ोल्डर): %s",
            "इंटरफ़ेस की भाषा|लॉग|पिछली जाँच की रिपोर्ट कॉपी करें|गेम फ़ोल्डर नहीं चुना गया|गेम फ़ोल्डर: %s|फ़ोल्डर चुनें|गेम: %s|गेम की भाषा|उपशीर्षक: %s|अंतर्निहित|"+
            "बाहरी|उपशीर्षक स्रोत|sub.yaml चुनें|अतिरिक्त उपशीर्षक|FPS दिखाएँ|FPS और फ़्रेम समय दिखाएँ|तेज़ आगे बढ़ाने के लिए दबाए रखें|परीक्षण मोड (सब अनलॉक)|गेम शुरू करें|रद्द करें|"+
            "सहेजा जा रहा है…|अगली बार के लिए सहेजा गया।|सहेजा नहीं जा सका: %s|पहले गेम फ़ोल्डर चुनें।|सेटिंग और सेव चुने गए फ़ोल्डर में रखे जाते हैं।|संसाधनों की जाँच…|%d आर्काइव मिले।|शुरू हो रहा है…|उपशीर्षक की जाँच…|उपशीर्षक चालू: %s|"+
            "उपशीर्षक चालू नहीं हुए: %s|रद्द; पुराने उपशीर्षक रखे गए।|रद्द; पुराना फ़ोल्डर रखा गया।|कार्य विफल: %s|सिस्टम फ़ाइल चयनकर्ता नहीं खुल सका।|गेम फ़ोल्डर की पढ़ने और लिखने की अनुमति फिर दें।|स्थायी पढ़ने की अनुमति वाली स्थानीय sub.yaml चुनें।|जाँच की रिपोर्ट पढ़ी जा रही है…|जाँच की रिपोर्ट कॉपी हो गई।|अभी रिपोर्ट नहीं है। गेम से सामान्य रूप से बाहर निकलकर फिर कोशिश करें।|जाँच विफल: %s"),
        entry("bn","বাংলা",false,"ওপরে ডান দিকের মেনু দেখান","স্ক্রিপ্ট PAK (গেমের ফোল্ডার): %s","ফন্ট PAK (গেমের ফোল্ডার): %s",
            "ইন্টারফেসের ভাষা|লগ|সর্বশেষ পরীক্ষার প্রতিবেদন কপি করুন|গেমের ফোল্ডার বাছা হয়নি|গেমের ফোল্ডার: %s|ফোল্ডার বাছুন|গেম: %s|গেমের ভাষা|সাবটাইটেল: %s|বিল্ট-ইন|"+
            "বাহ্যিক|সাবটাইটেলের উৎস|sub.yaml বাছুন|অতিরিক্ত সাবটাইটেল|FPS দেখান|FPS ও ফ্রেমের সময় দেখান|দ্রুত এগোতে চেপে রাখুন|পরীক্ষা মোড (সব আনলক)|গেম শুরু করুন|বাতিল|"+
            "সংরক্ষণ হচ্ছে…|পরেরবারের জন্য সংরক্ষিত।|সংরক্ষণ করা যায়নি: %s|আগে গেমের ফোল্ডার বাছুন।|সেটিং ও সেভ বাছা ফোল্ডারে থাকে।|রিসোর্স পরীক্ষা হচ্ছে…|%dটি আর্কাইভ পাওয়া গেছে।|চালু হচ্ছে…|সাবটাইটেল পরীক্ষা হচ্ছে…|সাবটাইটেল চালু: %s|"+
            "সাবটাইটেল চালু হয়নি: %s|বাতিল; আগের সাবটাইটেল রাখা হয়েছে।|বাতিল; আগের ফোল্ডার রাখা হয়েছে।|কাজটি ব্যর্থ: %s|সিস্টেমের ফাইল বাছাই খোলা যায়নি।|গেমের ফোল্ডারে পড়া ও লেখার অনুমতি আবার দিন।|স্থায়ীভাবে পড়ার অনুমতিসহ স্থানীয় sub.yaml বাছুন।|পরীক্ষার প্রতিবেদন পড়া হচ্ছে…|পরীক্ষার প্রতিবেদন কপি হয়েছে।|এখনও প্রতিবেদন নেই। গেম থেকে স্বাভাবিকভাবে বেরিয়ে আবার চেষ্টা করুন।|পরীক্ষা ব্যর্থ: %s"),
        entry("ur","اردو",true,"اوپر دائیں طرف مینو دکھائیں","اسکرپٹ PAK (گیم کا فولڈر): %s","فونٹ PAK (گیم کا فولڈر): %s",
            "انٹرفیس کی زبان|لاگ|آخری تشخیصی رپورٹ کاپی کریں|گیم کا فولڈر منتخب نہیں|گیم کا فولڈر: %s|فولڈر منتخب کریں|گیم: %s|گیم کی زبان|سب ٹائٹلز: %s|بلٹ اِن|"+
            "بیرونی|سب ٹائٹلز کا ماخذ|sub.yaml منتخب کریں|اضافی سب ٹائٹلز|FPS دکھائیں|FPS اور فریم کا وقت دکھائیں|تیزی سے آگے بڑھانے کے لیے دبائے رکھیں|ٹیسٹ موڈ (سب کھولیں)|گیم شروع کریں|منسوخ|"+
            "محفوظ ہو رہا ہے…|اگلے آغاز کے لیے محفوظ ہو گیا۔|محفوظ نہیں ہو سکا: %s|پہلے گیم کا فولڈر منتخب کریں۔|سیٹنگز اور سیو منتخب فولڈر میں رہتے ہیں۔|وسائل کی جانچ…|%d آرکائیو ملے۔|شروع ہو رہا ہے…|سب ٹائٹلز کی جانچ…|سب ٹائٹلز فعال: %s|"+
            "سب ٹائٹلز فعال نہیں ہوئے: %s|منسوخ؛ پچھلے سب ٹائٹلز برقرار ہیں۔|منسوخ؛ پچھلا فولڈر برقرار ہے۔|عمل ناکام: %s|سسٹم کا فائل انتخاب نہیں کھل سکا۔|گیم کے فولڈر کو پڑھنے اور لکھنے کی اجازت دوبارہ دیں۔|مستقل پڑھنے کی اجازت والا مقامی sub.yaml منتخب کریں۔|تشخیصی رپورٹ پڑھی جا رہی ہے…|تشخیصی رپورٹ کاپی ہو گئی۔|ابھی کوئی رپورٹ نہیں۔ گیم سے معمول کے مطابق نکل کر دوبارہ کوشش کریں۔|تشخیص ناکام: %s"),
        entry("id","Bahasa Indonesia",false,"Tampilkan menu kanan atas","PAK skrip (folder game): %s","PAK font (folder game): %s",
            "Bahasa antarmuka|Log|Salin diagnostik terakhir|Folder game belum dipilih|Folder game: %s|Pilih folder|Game: %s|Bahasa game|Takarir: %s|Bawaan|"+
            "Eksternal|Sumber takarir|Pilih sub.yaml|Takarir tambahan|Tampilkan FPS|Tampilkan FPS dan waktu bingkai|Tahan untuk mempercepat|Mode uji (buka semua)|Mulai game|Batal|"+
            "Menyimpan…|Disimpan untuk peluncuran berikutnya.|Gagal menyimpan: %s|Pilih folder game terlebih dahulu.|Pengaturan dan simpanan ada di folder pilihan.|Memeriksa sumber daya…|Ditemukan %d arsip.|Memulai…|Memeriksa takarir…|Takarir diaktifkan: %s|"+
            "Takarir tidak diaktifkan: %s|Dibatalkan; takarir tetap.|Dibatalkan; folder tetap.|Operasi gagal: %s|Pemilih file sistem tidak dapat dibuka.|Izinkan kembali baca/tulis pada folder game.|Pilih sub.yaml lokal dengan izin baca permanen.|Membaca diagnostik…|Diagnostik disalin.|Belum ada diagnostik. Keluar dari game secara normal lalu coba lagi.|Diagnostik gagal: %s"),
        entry("ms","Bahasa Melayu",false,"Papar menu kanan atas","PAK skrip (folder permainan): %s","PAK fon (folder permainan): %s",
            "Bahasa antara muka|Log|Salin diagnostik terakhir|Folder permainan belum dipilih|Folder permainan: %s|Pilih folder|Permainan: %s|Bahasa permainan|Sari kata: %s|Terbina dalam|"+
            "Luaran|Sumber sari kata|Pilih sub.yaml|Sari kata tambahan|Papar FPS|Papar FPS dan masa bingkai|Tekan lama untuk laju|Mod ujian (buka semua)|Mula permainan|Batal|"+
            "Menyimpan…|Disimpan untuk pelancaran seterusnya.|Gagal menyimpan: %s|Pilih folder permainan dahulu.|Tetapan dan simpanan berada dalam folder pilihan.|Menyemak sumber…|%d arkib ditemui.|Memulakan…|Menyemak sari kata…|Sari kata diaktifkan: %s|"+
            "Sari kata tidak diaktifkan: %s|Dibatalkan; sari kata dikekalkan.|Dibatalkan; folder dikekalkan.|Operasi gagal: %s|Pemilih fail sistem tidak dapat dibuka.|Benarkan semula akses baca/tulis ke folder permainan.|Pilih sub.yaml setempat dengan kebenaran baca kekal.|Membaca diagnostik…|Diagnostik disalin.|Belum ada diagnostik. Keluar daripada permainan secara biasa dan cuba lagi.|Diagnostik gagal: %s"),
        entry("vi","Tiếng Việt",false,"Hiện menu góc trên bên phải","PAK kịch bản (thư mục trò chơi): %s","PAK phông chữ (thư mục trò chơi): %s",
            "Ngôn ngữ giao diện|Nhật ký|Sao chép chẩn đoán gần nhất|Chưa chọn thư mục trò chơi|Thư mục trò chơi: %s|Chọn thư mục|Trò chơi: %s|Ngôn ngữ trò chơi|Phụ đề: %s|Tích hợp|"+
            "Bên ngoài|Nguồn phụ đề|Chọn sub.yaml|Phụ đề bổ sung|Hiện FPS|Hiện FPS và thời gian khung hình|Nhấn giữ để tua nhanh|Chế độ thử (mở khóa tất cả)|Bắt đầu chơi|Hủy|"+
            "Đang lưu…|Đã lưu cho lần khởi động sau.|Không thể lưu: %s|Hãy chọn thư mục trò chơi trước.|Cài đặt và bản lưu ở trong thư mục đã chọn.|Đang kiểm tra tài nguyên…|Tìm thấy %d tệp gói.|Đang khởi động…|Đang kiểm tra phụ đề…|Đã bật phụ đề: %s|"+
            "Chưa bật phụ đề: %s|Đã hủy; giữ nguyên phụ đề.|Đã hủy; giữ nguyên thư mục.|Thao tác thất bại: %s|Không mở được trình chọn tệp hệ thống.|Hãy cấp lại quyền đọc/ghi cho thư mục trò chơi.|Chọn sub.yaml cục bộ có quyền đọc lâu dài.|Đang đọc chẩn đoán…|Đã sao chép chẩn đoán.|Chưa có chẩn đoán. Hãy thoát trò chơi bình thường rồi thử lại.|Chẩn đoán thất bại: %s"),
        entry("th","ไทย",false,"แสดงเมนูด้านขวาบน","PAK บทเกม (โฟลเดอร์เกม): %s","PAK แบบอักษร (โฟลเดอร์เกม): %s",
            "ภาษาหน้าจอ|บันทึก|คัดลอกผลวินิจฉัยล่าสุด|ยังไม่ได้เลือกโฟลเดอร์เกม|โฟลเดอร์เกม: %s|เลือกโฟลเดอร์|เกม: %s|ภาษาเกม|คำบรรยาย: %s|ในตัว|"+
            "ภายนอก|แหล่งคำบรรยาย|เลือก sub.yaml|คำบรรยายเสริม|แสดง FPS|แสดง FPS และเวลาเฟรม|กดค้างเพื่อเร่ง|โหมดทดสอบ (ปลดล็อกทั้งหมด)|เริ่มเกม|ยกเลิก|"+
            "กำลังบันทึก…|บันทึกแล้ว มีผลเมื่อเปิดครั้งถัดไป|บันทึกไม่สำเร็จ: %s|โปรดเลือกโฟลเดอร์เกมก่อน|การตั้งค่าและเซฟอยู่ในโฟลเดอร์ที่เลือก|กำลังตรวจสอบทรัพยากร…|พบไฟล์แพ็ก %d ไฟล์|กำลังเริ่ม…|กำลังตรวจสอบคำบรรยาย…|เปิดคำบรรยายแล้ว: %s|"+
            "ยังไม่ได้เปิดคำบรรยาย: %s|ยกเลิกแล้ว คงคำบรรยายเดิม|ยกเลิกแล้ว คงโฟลเดอร์เดิม|ดำเนินการไม่สำเร็จ: %s|เปิดตัวเลือกไฟล์ของระบบไม่ได้|โปรดอนุญาตให้อ่านและเขียนโฟลเดอร์เกมอีกครั้ง|เลือก sub.yaml ในเครื่องที่อนุญาตให้อ่านได้ถาวร|กำลังอ่านผลวินิจฉัย…|คัดลอกผลวินิจฉัยแล้ว|ยังไม่มีผลวินิจฉัย โปรดออกจากเกมตามปกติแล้วลองใหม่|วินิจฉัยไม่สำเร็จ: %s"),
        entry("fil","Filipino",false,"Ipakita ang menu sa kanang itaas","Script PAK (game folder): %s","Font PAK (game folder): %s",
            "Wika ng interface|Log|Kopyahin ang huling diagnostic|Walang napiling game folder|Game folder: %s|Pumili ng folder|Laro: %s|Wika ng laro|Subtitle: %s|Built-in|"+
            "Panlabas|Pinagmulan ng subtitle|Pumili ng sub.yaml|Dagdag na subtitle|Ipakita ang FPS|Ipakita ang FPS at oras ng frame|Pindutin nang matagal para bumilis|Test mode (i-unlock lahat)|Simulan ang laro|Kanselahin|"+
            "Sine-save…|Na-save para sa susunod na pagbukas.|Hindi na-save: %s|Pumili muna ng game folder.|Nasa napiling folder ang settings at saves.|Sinusuri ang resources…|Nakakita ng %d archive.|Binubuksan…|Sinusuri ang subtitle…|Naka-enable ang subtitle: %s|"+
            "Hindi na-enable ang subtitle: %s|Kinansela; hindi binago ang subtitle.|Kinansela; hindi binago ang folder.|Nabigo ang operasyon: %s|Hindi mabuksan ang system file picker.|Payagan muli ang pagbasa at pagsulat sa game folder.|Pumili ng lokal na sub.yaml na may permanenteng pahintulot sa pagbasa.|Binabasa ang diagnostic…|Nakopya ang diagnostic.|Wala pang diagnostic. Lumabas nang normal sa laro at subukang muli.|Nabigo ang diagnostic: %s"),
        entry("ta","தமிழ்",false,"மேல் வலது பட்டியலைக் காட்டு","ஸ்கிரிப்ட் PAK (விளையாட்டுக் கோப்புறை): %s","எழுத்துரு PAK (விளையாட்டுக் கோப்புறை): %s",
            "இடைமுக மொழி|பதிவு|கடைசி ஆய்வறிக்கையை நகலெடு|விளையாட்டுக் கோப்புறை தேர்வாகவில்லை|விளையாட்டுக் கோப்புறை: %s|கோப்புறையைத் தேர்வு செய்|விளையாட்டு: %s|விளையாட்டு மொழி|வசனம்: %s|உள்ளமைந்தது|"+
            "வெளிப்புறம்|வசன மூலம்|sub.yaml தேர்வு செய்|கூடுதல் வசனங்கள்|FPS காட்டு|FPS மற்றும் சட்டக நேரத்தைக் காட்டு|விரைவாக நகர அழுத்திப் பிடி|சோதனை முறை (அனைத்தையும் திற)|விளையாட்டைத் தொடங்கு|ரத்து செய்|"+
            "சேமிக்கிறது…|அடுத்த தொடக்கத்திற்காகச் சேமிக்கப்பட்டது.|சேமிக்க முடியவில்லை: %s|முதலில் விளையாட்டுக் கோப்புறையைத் தேர்வு செய்யவும்.|அமைப்புகளும் சேமிப்புகளும் தேர்ந்த கோப்புறையில் இருக்கும்.|வளங்களைச் சரிபார்க்கிறது…|%d காப்பகங்கள் கிடைத்தன.|தொடங்குகிறது…|வசனங்களைச் சரிபார்க்கிறது…|வசனங்கள் இயக்கப்பட்டன: %s|"+
            "வசனங்கள் இயக்கப்படவில்லை: %s|ரத்து செய்யப்பட்டது; பழைய வசனங்கள் தொடரும்.|ரத்து செய்யப்பட்டது; பழைய கோப்புறை தொடரும்.|செயல்பாடு தோல்வி: %s|கணினியின் கோப்புத் தேர்வியைத் திறக்க முடியவில்லை.|விளையாட்டுக் கோப்புறைக்கு மீண்டும் படிக்கவும் எழுதவும் அனுமதி அளிக்கவும்.|நிரந்தர வாசிப்பு அனுமதியுள்ள உள்ளூர் sub.yaml ஐத் தேர்வு செய்யவும்.|ஆய்வறிக்கையைப் படிக்கிறது…|ஆய்வறிக்கை நகலெடுக்கப்பட்டது.|இன்னும் அறிக்கை இல்லை. விளையாட்டிலிருந்து வழக்கமாக வெளியேறி மீண்டும் முயலவும்.|ஆய்வு தோல்வி: %s")
    };
    static {
        Set<String> tags=new HashSet<>(),names=new HashSet<>();
        for(Entry e:ENTRIES){
            if(!tags.add(e.tag) || !names.add(e.name) || e.words.length!=Key.values().length)throw new IllegalStateException("Invalid launcher language: "+e.tag);
            for(int n=0;n<e.words.length;++n)if(e.words[n].isEmpty() || !signature(e.words[n]).equals(signature(ENTRIES[0].words[n])))throw new IllegalStateException("Invalid launcher translation: "+e.tag+"/"+Key.values()[n]);
        }
    }
    private final int index;
    private LauncherText(int index){this.index=index;}
    static String signature(String value){
        StringBuilder out=new StringBuilder();for(int n=0;n<value.length();++n)if(value.charAt(n)=='%'){
            if(++n==value.length() || (value.charAt(n)!='s' && value.charAt(n)!='d'))throw new IllegalArgumentException("Invalid launcher format");out.append(value.charAt(n));
        }return out.toString();
    }
    static LauncherText forTag(String tag){
        if(tag==null)return new LauncherText(2);Locale locale=Locale.forLanguageTag(tag);String language=locale.getLanguage();
        if(language.equals("zh"))return new LauncherText(locale.getScript().equals("Hant") || (locale.getScript().isEmpty() && (locale.getCountry().equals("TW") || locale.getCountry().equals("HK") || locale.getCountry().equals("MO")))?1:0);
        if(language.equals("tl"))language="fil";
        for(int n=0;n<ENTRIES.length;++n)if(Locale.forLanguageTag(ENTRIES[n].tag).getLanguage().equals(language))return new LauncherText(n);
        return new LauncherText(2);
    }
    // Only an explicit user choice overrides the current system language.
    static LauncherText resolve(String savedTag,String systemTag){return forTag(savedTag==null || savedTag.isEmpty()?systemTag:savedTag);}
    static LauncherText at(int index){if(index<0 || index>=ENTRIES.length)throw new IllegalArgumentException("Unknown interface language");return new LauncherText(index);}
    static String[] names(){String[] result=new String[ENTRIES.length];for(int n=0;n<result.length;++n)result[n]=ENTRIES[n].name;return result;}
    static int count(){return ENTRIES.length;}
    int index(){return index;}
    String tag(){return ENTRIES[index].tag;}
    String name(){return ENTRIES[index].name;}
    boolean rtl(){return ENTRIES[index].rtl;}
    String text(Key key,Object... args){
        String template=ENTRIES[index].words[key.ordinal()];
        if(key==Key.CHOOSE_SUBTITLE || key==Key.REQUIRE_LOCAL_SUBTITLE)template=template.replace("sub.yaml","sub.yaml / Todokanai");
        return String.format(Locale.forLanguageTag(tag()),template,args);
    }
}
