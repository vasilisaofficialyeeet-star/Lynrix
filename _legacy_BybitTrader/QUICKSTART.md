# BybitTrader — Быстрый старт

## ✅ Приложение готово к использованию!

Приложение скопировано на рабочий стол: `~/Desktop/BybitTrader.app`

---

## 🚀 Запуск

### Способ 1: Двойной клик
Откройте `BybitTrader.app` на рабочем столе

### Способ 2: Из терминала
```bash
open ~/Desktop/BybitTrader.app
```

### Способ 3: Пересборка из исходников
```bash
cd ~/CLionProjects/bybit/BybitTrader
./build.sh release
open ~/Library/Developer/Xcode/DerivedData/BybitTrader-*/Build/Products/Release/BybitTrader.app
```

---

## 📱 Интерфейс

После запуска вы увидите 7 вкладок в sidebar:

1. **Dashboard** — основные метрики, PnL, latency
2. **Order Book** — стакан заявок с глубиной
3. **Trade Tape** — лента сделок с aggressor side
4. **Portfolio** — позиция, PnL breakdown, статистика
5. **Signals** — история торговых сигналов
6. **Settings** — настройки, API ключи, параметры
7. **Logs** — системные логи с фильтрацией

---

## ⚙️ Первоначальная настройка

### 1. Добавьте API ключи (опционально)
- Откройте вкладку **Settings**
- Введите API Key и API Secret
- Нажмите **Save to Keychain**
- Ключи сохраняются в macOS Keychain (безопасно)

### 2. Выберите режим
- **PAPER** — симуляция торговли (по умолчанию)
- **LIVE** — реальная торговля (требует API ключи)

### 3. Запустите движок
- Нажмите кнопку **Start** в sidebar
- Или используйте горячую клавишу **⌘R**

---

## ⌨️ Горячие клавиши

| Клавиша | Действие |
|---------|----------|
| **⌘R** | Start/Stop engine |
| **⇧⌘T** | Toggle Paper/Live mode |
| **⇧⌘.** | Emergency Stop (паника) |
| **⌘K** | Clear logs |
| **⇧⌘E** | Export logs |

---

## 📊 Экспорт данных

Меню **Export** содержит:
- **Export Logs** — текстовый файл с логами
- **Export Trades CSV** — CSV файл со сделками
- **Export Signals CSV** — CSV файл с сигналами

---

## 🔧 Параметры торговли

В **Settings** можно настроить:
- Symbol (по умолчанию: BTCUSDT)
- Order Qty — размер ордера
- Signal Threshold — порог уверенности модели
- Max Position Size — максимальная позиция
- Max Daily Loss — максимальный дневной убыток
- Risk параметры

---

## 🛡️ Безопасность

- ✅ API ключи хранятся в **macOS Keychain**
- ✅ Никогда не сохраняются в файлы
- ✅ Paper mode по умолчанию
- ✅ Emergency Stop доступен всегда
- ✅ Panic handler при критических ошибках

---

## 🐛 Решение проблем

### Приложение не запускается
```bash
# Пересоберите приложение
cd ~/CLionProjects/bybit/BybitTrader
./build.sh release
```

### Нет подключения к WebSocket
- Проверьте интернет-соединение
- Проверьте логи (вкладка Logs)
- Убедитесь, что API ключи корректны (для private каналов)

### Приложение крашится
```bash
# Запустите из терминала для просмотра ошибок
~/Desktop/BybitTrader.app/Contents/MacOS/BybitTrader
```

---

## 📁 Структура проекта

```
bybit/
├── src/                    # C++ trading engine
│   ├── app/               # Application core
│   ├── orderbook/         # Order book engine
│   ├── bridge/            # C API wrapper
│   └── ...
├── BybitTrader/           # macOS app
│   ├── App/               # SwiftUI app entry
│   ├── Views/             # UI views
│   ├── Models/            # Swift models
│   ├── Bridge/            # Obj-C++ bridge
│   └── project.yml        # Xcode project config
└── build/                 # C++ build artifacts
```

---

## 🔄 Обновление приложения

После изменений в коде:

```bash
cd ~/CLionProjects/bybit/BybitTrader
./build.sh release
cp -R ~/Library/Developer/Xcode/DerivedData/BybitTrader-*/Build/Products/Release/BybitTrader.app ~/Desktop/
```

---

## 📞 Поддержка

Все исходники доступны в:
- C++ engine: `~/CLionProjects/bybit/src/`
- macOS app: `~/CLionProjects/bybit/BybitTrader/`

Логи приложения: вкладка **Logs** или `./logs/` директория

---

**Приложение готово к использованию! 🎉**
