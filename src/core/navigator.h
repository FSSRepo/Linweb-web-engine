#pragma once
#include <vector>
#include <string>

namespace linweb {

/**
 * @brief Sistema de navegación entre páginas HTML locales.
 *
 * Mantiene un historial de URLs visitadas y permite navegar
 * hacia adelante y hacia atrás.
 */
class Navigator {
private:
    std::vector<std::string> history;
    size_t current_index;
    std::string current_url;

public:
    Navigator();

    /** Carga una nueva página HTML y la agrega al historial */
    void load_page(const std::string& url);

    /** Navega atrás en el historial */
    void go_back();

    /** Navega adelante en el historial */
    void go_forward();

    /** Retorna la URL actual */
    std::string get_current_url() const;

    /** Retorna el historial completo de navegación */
    const std::vector<std::string>& get_history() const;

    /** Verifica si puede ir atrás */
    bool can_go_back() const;

    /** Verifica si puede ir adelante */
    bool can_go_forward() const;
};

} // namespace linweb
