#pragma once
#include <vector>
#include <string>
#include <memory>
#include "core/dom.h"

namespace linweb {

/** Cuenta todos los nodos en el subárbol de forma recursiva */
size_t count_dom_nodes(const std::shared_ptr<Node>& node);

/** Busca el primer nodo elemento con el id especificado */
std::shared_ptr<Node> find_node_by_id(const std::shared_ptr<Node>& node, const std::string& id);

/** Busca todos los nodos elemento con la etiqueta especificada */
std::vector<std::shared_ptr<ElementNode>> find_nodes_by_tag(const std::shared_ptr<Node>& node, const std::string& tag_name);

/** Busca todos los nodos elemento que tengan la clase especificada */
std::vector<std::shared_ptr<ElementNode>> find_nodes_by_class(const std::shared_ptr<Node>& node, const std::string& class_name);

/** Extrae todo el contenido de texto concatenado recursivamente */
std::string get_text_content(const std::shared_ptr<Node>& node);

/** Serializa los hijos del nodo a una cadena HTML */
std::string get_inner_html(const std::shared_ptr<Node>& node);

/** Parsea HTML y reemplaza los hijos del nodo con el resultado */
void set_inner_html(const std::shared_ptr<Node>& node, const std::string& html);

/** Filtra y devuelve solo los hijos que son elementos */
std::vector<std::shared_ptr<Node>> get_element_children(const std::shared_ptr<Node>& node);

/** Filtra y devuelve solo los hijos que son nodos de texto */
std::vector<std::shared_ptr<Node>> get_text_children(const std::shared_ptr<Node>& node);

} // namespace linweb
