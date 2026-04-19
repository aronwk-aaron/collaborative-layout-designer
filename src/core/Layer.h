#pragma once

#include "Ids.h"

#include <string>

namespace cld::core {

enum class LayerKind {
    Grid,
    Brick,
    Text,
    Area,
    Ruler,
    AnchoredText,
};

class Layer {
public:
    virtual ~Layer() = default;

    LayerId     id() const { return id_; }
    LayerKind   kind() const { return kind_; }
    const std::string& name() const { return name_; }
    bool        visible() const { return visible_; }

    void setName(std::string name) { name_ = std::move(name); }
    void setVisible(bool v) { visible_ = v; }

protected:
    Layer(LayerId id, LayerKind kind, std::string name)
        : id_(id), kind_(kind), name_(std::move(name)) {}

private:
    LayerId     id_{};
    LayerKind   kind_ = LayerKind::Brick;
    std::string name_;
    bool        visible_ = true;
};

}
