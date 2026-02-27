// Состояние соединения
let connected = true;
let reconnectAttempts = 0;

// Загрузка данных при открытии страницы
document.addEventListener('DOMContentLoaded', function() {
    updateStatus();
    setInterval(updateStatus, 1000); // Обновление каждую секунду
});

// Функция обновления статуса
function updateStatus() {
    fetch('/api/status')
        .then(response => {
            if (!response.ok) throw new Error('Network error');
            return response.json();
        })
        .then(data => {
            connected = true;
            reconnectAttempts = 0;
            updateConnectionStatus(true);
            updateUI(data);
        })
        .catch(error => {
            console.error('Error:', error);
            connected = false;
            reconnectAttempts++;
            updateConnectionStatus(false);
            
            // Пытаемся переподключиться
            if (reconnectAttempts < 10) {
                setTimeout(updateStatus, 2000);
            }
        });
}

// Обновление UI с полученными данными
function updateUI(data) {
    // Основная информация
    document.getElementById('currentWeight').textContent = data.currentWeight + ' г';
    document.getElementById('emptyWeight').textContent = data.emptyWeight + ' г';
    document.getElementById('waterVolume').textContent = data.waterVolume + ' мл';
    document.getElementById('cups').textContent = data.cups;
    
    // Прогресс-бар
    const percentage = (data.waterVolume / data.maxVolume) * 100;
    document.getElementById('progressBar').style.width = percentage + '%';
    
    // Состояние системы
    document.getElementById('systemState').textContent = data.systemState;
    document.getElementById('kettlePresent').textContent = data.kettlePresent ? '✅ Есть' : '❌ Нет';
    document.getElementById('wifiSignal').textContent = data.wifiSignal + ' dBm';
    document.getElementById('mqttStatus').textContent = data.mqttConnected ? '✅ Подключен' : '❌ Отключен';
    
    // Калибровка
    document.getElementById('calibrationFactor').textContent = data.calibrationFactor.toFixed(6);
    document.getElementById('calibrationStatus').textContent = data.calibrationDone ? '✅ Готова' : '❌ Не выполнена';
    
    // Статистика
    document.getElementById('mqttSent').textContent = data.mqttSent;
    document.getElementById('mqttFailed').textContent = data.mqttFailed;
    document.getElementById('uptime').textContent = formatUptime(data.uptime);
    document.getElementById('freeHeap').textContent = formatBytes(data.freeHeap);
}

// Обновление статуса соединения
function updateConnectionStatus(isConnected) {
    const statusEl = document.getElementById('connectionStatus');
    if (isConnected) {
        statusEl.className = 'connection-status online';
        statusEl.textContent = '🟢 WiFi подключен';
    } else {
        statusEl.className = 'connection-status offline';
        statusEl.textContent = '🔴 Соединение потеряно. Переподключение...';
    }
}

// Форматирование времени работы
function formatUptime(ms) {
    const seconds = Math.floor(ms / 1000);
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    return hours + ' ч ' + minutes + ' мин';
}

// Форматирование байтов
function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

// Команды управления
function fillCups(count) {
    fetch('/api/fill?cups=' + count, { method: 'POST' })
        .then(response => {
            if (response.ok) {
                showNotification('Налив ' + count + ' кружки(ек) запущен');
            }
        });
}

function fillFull() {
    fetch('/api/fill?full=1', { method: 'POST' })
        .then(response => {
            if (response.ok) {
                showNotification('Налив полного чайника запущен');
            }
        });
}

function fillCustom() {
    const ml = document.getElementById('customML').value;
    if (ml && ml > 0 && ml <= 1700) {
        fetch('/api/fill?ml=' + ml, { method: 'POST' })
            .then(response => {
                if (response.ok) {
                    showNotification('Налив ' + ml + ' мл запущен');
                }
            });
    } else {
        alert('Введите корректный объем от 50 до 1700 мл');
    }
}

function stopFill() {
    fetch('/api/stop', { method: 'POST' })
        .then(response => {
            if (response.ok) {
                showNotification('Налив остановлен');
            }
        });
}

function startCalibration() {
    if (confirm('Запустить калибровку пустого чайника?')) {
        fetch('/api/calibrate', { method: 'POST' })
            .then(response => {
                if (response.ok) {
                    showNotification('Калибровка запущена');
                }
            });
    }
}

// Временное уведомление
function showNotification(message) {
    const notification = document.createElement('div');
    notification.className = 'notification';
    notification.textContent = message;
    notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        background: #4CAF50;
        color: white;
        padding: 15px 20px;
        border-radius: 5px;
        box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        z-index: 1000;
        animation: slideIn 0.3s ease;
    `;
    
    document.body.appendChild(notification);
    
    setTimeout(() => {
        notification.style.animation = 'slideOut 0.3s ease';
        setTimeout(() => {
            document.body.removeChild(notification);
        }, 300);
    }, 3000);
}

function showChangePasswordDialog() {
    const modal = document.createElement('div');
    modal.className = 'modal';
    modal.innerHTML = `
        <div class="modal-content">
            <h3>Смена пароля</h3>
            <input type="password" id="oldPassword" placeholder="Старый пароль">
            <input type="password" id="newPassword" placeholder="Новый пароль (мин. 4 символа)">
            <input type="password" id="confirmPassword" placeholder="Подтвердите пароль">
            <div class="modal-buttons">
                <button onclick="changePassword()">Сохранить</button>
                <button onclick="closeModal()">Отмена</button>
            </div>
        </div>
    `;
    document.body.appendChild(modal);
}

function changePassword() {
    const oldPass = document.getElementById('oldPassword').value;
    const newPass = document.getElementById('newPassword').value;
    const confirmPass = document.getElementById('confirmPassword').value;
    
    fetch('/change-password', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: `oldPassword=${encodeURIComponent(oldPass)}&newPassword=${encodeURIComponent(newPass)}&confirmPassword=${encodeURIComponent(confirmPass)}`
    })
    .then(r => r.json())
    .then(data => {
        if (data.success) {
            alert('✅ Пароль успешно изменен!');
            closeModal();
        } else {
            alert('❌ Ошибка: ' + data.message);
        }
    });
}

function closeModal() {
    const modal = document.querySelector('.modal');
    if (modal) modal.remove();
}

// Добавляем стили анимации
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from { transform: translateX(100%); opacity: 0; }
        to { transform: translateX(0); opacity: 1; }
    }
    
    @keyframes slideOut {
        from { transform: translateX(0); opacity: 1; }
        to { transform: translateX(100%); opacity: 0; }
    }
`;
document.head.appendChild(style);