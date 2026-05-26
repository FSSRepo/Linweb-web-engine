document.addEventListener('DOMContentLoaded', function() {
    console.log('App loaded');
    const statusElement = document.getElementById('status');
    statusElement.innerText = 'Aplicación lista para webOS';

    // Manejar eventos de webOS si están disponibles
    if (window.PalmSystem) {
        console.log('webOS detectado');
        statusElement.innerText += ' (webOS detectado)';
    }

    // Datos de los submenús
    const subMenuData = {
        'System': ['System Settings', 'Network Settings', 'System Update', 'Users'],
        'Games': ['Store', 'Library', 'Last Played', 'Trophies'],
        'Multimedia': ['Music Player', 'Photo Gallery', 'Video Player', 'YouTube'],
        'DVD': ['Play Disc', 'Disc Info', 'Eject']
    };

    // Manejo de navegación del menú
    const menuItems = document.querySelectorAll('.menu-item');
    const mainMenu = document.getElementById('main-menu');
    const subMenuContainer = document.getElementById('submenu-container');
    let currentIndex = 0;
    let subMenuIndex = -1; // -1 significa que estamos en el menú principal

    function updateActiveItem(index) {
        // En el XMB, el ítem activo permanece en una posición fija
        // y el contenedor se desplaza.
        // Calculamos el desplazamiento basado en el índice
        const itemWidth = 150; // Ancho aproximado de cada item + margen
        const offset = 300 - (index * itemWidth); 
        mainMenu.style.transform = `translateX(${offset}px)`;

        menuItems.forEach((item, i) => {
            if (i === index) {
                item.classList.add('active');
                statusElement.innerText = `${item.getAttribute('data-name').toUpperCase()}`;
                loadSubMenu(item.getAttribute('data-name'));
            } else {
                item.classList.remove('active');
            }
        });
    }

    function loadSubMenu(category) {
        subMenuContainer.innerHTML = '';
        const items = subMenuData[category] || [];
        
        items.forEach((text, index) => {
            const div = document.createElement('div');
            div.className = 'submenu-item';
            div.innerText = text;
            subMenuContainer.appendChild(div); 
        });
        
        subMenuIndex = -1; // Resetear al cambiar de categoría
        updateSubMenuHighlight();
    }

    function updateSubMenuHighlight() {
        const subItems = document.querySelectorAll('.submenu-item');
        subItems.forEach((item, i) => {
            if (i === subMenuIndex) {
                item.classList.add('active');
            } else {
                item.classList.remove('active');
            }

            // Marcar items por encima del activo para el desplazamiento
            if (subMenuIndex >= 0 && i < subMenuIndex) {
                item.classList.add('above-active');
            } else {
                item.classList.remove('above-active');
            }
        });

        // Desplazamiento vertical del submenú
        if (subMenuIndex >= 0) {
            const offset = -(subMenuIndex * 50); // 50px es la altura de cada submenu-item
            subMenuContainer.style.transform = `translateY(${offset}px)`;
        } else {
            subMenuContainer.style.transform = `translateY(0)`;
        }
    }

    // Inicializar estado
    updateActiveItem(currentIndex);

    // Ejemplo de manejo de teclas para control remoto
    document.addEventListener('keydown', function(e) {
        console.log('Tecla presionada:', e.keyCode);
        
        if (e.keyCode === 37) { // Left
            if (subMenuIndex === -1) {
                currentIndex = (currentIndex > 0) ? currentIndex - 1 : menuItems.length - 1;
                updateActiveItem(currentIndex);
            }
        } else if (e.keyCode === 39) { // Right
            if (subMenuIndex === -1) {
                currentIndex = (currentIndex < menuItems.length - 1) ? currentIndex + 1 : 0;
                updateActiveItem(currentIndex);
            }
        } else if (e.keyCode === 38) { // Up
            const items = subMenuData[menuItems[currentIndex].getAttribute('data-name')];
            if (subMenuIndex > -1) {
                subMenuIndex--;
                updateSubMenuHighlight();
            }
        } else if (e.keyCode === 40) { // Down
            const items = subMenuData[menuItems[currentIndex].getAttribute('data-name')];
            if (subMenuIndex < items.length - 1) {
                subMenuIndex++;
                updateSubMenuHighlight();
            }
        } else if (e.keyCode === 13) { // OK / Enter
            if (subMenuIndex === -1) {
                subMenuIndex = 0;
                updateSubMenuHighlight();
            } else {
                const items = subMenuData[menuItems[currentIndex].getAttribute('data-name')];
                console.log('Seleccionado:', items[subMenuIndex]);
                statusElement.innerText = `Ejecutando ${items[subMenuIndex]}...`;
            }
        } else if (e.keyCode === 461 || e.keyCode === 27) { // Back (webOS o ESC)
            if (subMenuIndex > -1) {
                subMenuIndex = -1;
                updateSubMenuHighlight();
            }
        }
    });

    // También permitir clics
    menuItems.forEach((item, index) => {
        item.addEventListener('click', () => {
            currentIndex = index;
            updateActiveItem(currentIndex);
        });
    });
});
