# PS3 Steward OS — vista previa en navegador

Este ejemplo es una copia de `steward-os/examples/linweb/ps3`, con un adaptador
para ejecutarlo sin el puente nativo de Steward OS. Abre `index.html` en un
navegador o sirve la carpeta, por ejemplo:

```powershell
python -m http.server 8000 --directory examples/ps3--os
```

Visita `http://localhost:8000/`. Usa las flechas para navegar, Enter para
seleccionar y Escape para volver. El rótulo **BROWSER PREVIEW · SIMULATED
SYSTEM** identifica el modo de demostración.

`js/browser-mocks.js` solo instala el adaptador si no existe el puente `linweb`.
El disco, sus archivos, las redes Wi-Fi, la música y el vídeo son simulados y
viven en memoria; se reinician al recargar. La escritura afecta únicamente al
disco simulado y el formateo está desactivado. Node Lite, el servidor web real y
la lectura de archivos del sistema informan que no están disponibles. Los
reproductores avanzan un reloj, sin sonido ni imagen reales: hay tres MP3 de
ejemplo en `/mnt/music` y un MP4 en `/mnt/video`, la misma disposición que crea
`make-iso.sh --music --video`. El Video Player abre directamente `/mnt/video`;
en el Music Player entra en la carpeta `music`.

Las opciones de almacenamiento, red, música, vídeo, Node Lite y diagnóstico
usan la columna del XMB. Cada opción lleva un pictograma SVG propio (carpeta
`icons/`) a la izquierda del texto. Las flechas cambian la selección y Escape
vuelve al nivel anterior; la lista se desplaza bajo una ventana recortada para
mantener visible la opción seleccionada sin invadir la fila de iconos.
