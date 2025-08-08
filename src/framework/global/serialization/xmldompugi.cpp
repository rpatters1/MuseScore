/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "xmldom.h"

#include <chrono>
#include <iostream>
#include <type_traits>
#include <cstring>

#include "pugixml.hpp"

#include "log.h"

using namespace muse;

struct muse::XmlDomImplData
{
    pugi::xml_document doc;
    pugi::xml_parse_result result{};
    bool triedload = false;

    using clock = std::chrono::steady_clock;
    clock::time_point created = clock::now();

    ~XmlDomImplData() {
        const auto elapsed = clock::now() - created;
        double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        std::cout << "[XmlDom] document lived " << ms << " ms\n";
    }
};

// ================================================
// generic pack/unpack
// ================================================

namespace {
template <class T>
inline xml_handle pack_handle(const T& t) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Backend handle must be trivially copyable");
    static_assert(sizeof(T) <= sizeof(xml_handle),
                  "Increase xml_handle slots/size");
    xml_handle h{};                   // zero both slots
    if (t) {
        std::memcpy(&h, &t, sizeof(T));  // copy only the bytes T needs
    }
    return h;
}

template <class T>
inline T unpack_handle(xml_handle h) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Backend handle must be trivially copyable");
    static_assert(sizeof(T) <= sizeof(xml_handle),
                  "Increase xml_handle slots/size");
    T t{};                            // zero-init destination
    std::memcpy(&t, &h, sizeof(T));   // copy back only sizeof(T)
    return t;
}
} // anonymous namespace

// ================================================
// XmlDomNode
// ================================================


XmlDomNode::XmlDomNode(const std::shared_ptr<XmlDomImplData>& xml, xml_node_handle node)
    : m_xml(xml), m_node(node)
{
}

bool XmlDomNode::isNull() const
{
    return !static_cast<bool>(m_node);
}

String XmlDomNode::nodeName() const
{
    if (!m_node) {
        return String();
    }

    pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node);
    switch (n.type()) {
    case pugi::node_element:
    case pugi::node_pi:
    case pugi::node_declaration:
    case pugi::node_doctype:
    case pugi::node_document: // usually empty
        return String::fromUtf8(n.name());
    case pugi::node_pcdata:
    case pugi::node_cdata:
        return String::fromUtf8(n.value());
    case pugi::node_comment:
        return String::fromUtf8(n.value()); // matches tiny’s Value() for comments
    case pugi::node_null:
    default:
        return String();
    }
}

bool XmlDomNode::hasChildNodes() const
{
    return m_node ? static_cast<bool>(unpack_handle<pugi::xml_node>(m_node).first_child()) : false;
}

XmlDomNode XmlDomNode::firstChild() const
{
    if (!m_node) {
        return XmlDomNode(m_xml, xml_node_handle());
    }
    pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node).first_child();
    return XmlDomNode(m_xml, pack_handle(n));
}

XmlDomElement XmlDomNode::firstChildElement(const char* name) const
{
    if (m_node) {
        pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node);
        pugi::xml_node c = name ? n.child(name)
                                : n.find_child([](pugi::xml_node x){ return x.type() == pugi::node_element; });
        if (c && c.type() == pugi::node_element) {
            return XmlDomElement(m_xml, pack_handle(c));
        }
    }
    return XmlDomElement(m_xml, xml_node_handle());
}

XmlDomNode XmlDomNode::nextSibling() const
{
    if (!m_node) {
        return XmlDomNode(m_xml, xml_node_handle());
    }
    pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node).next_sibling();
    return XmlDomNode(m_xml, pack_handle(n));
}

XmlDomNode XmlDomNode::previousSibling() const
{
    if (!m_node) {
        return XmlDomNode(m_xml, xml_node_handle());
    }
    pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node).previous_sibling();
    return XmlDomNode(m_xml, pack_handle(n));
}

XmlDomNode XmlDomNode::parent() const
{
    if (!m_node) {
        return XmlDomNode(m_xml, xml_node_handle());
    }
    pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node).parent();
    return XmlDomNode(m_xml, pack_handle(n));
}

XmlDomElement XmlDomNode::nextSiblingElement(const char* name) const
{
    if (m_node) {
        pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node);
        pugi::xml_node s = name ? n.next_sibling(name) : n.next_sibling();
        if (!name) {
            while (s && s.type() != pugi::node_element) s = s.next_sibling();
        }
        if (s && s.type() == pugi::node_element) {
            return XmlDomElement(m_xml, pack_handle(s));
        }
    }
    return XmlDomElement(m_xml, xml_node_handle());
}

XmlDomElement XmlDomNode::previousSiblingElement(const char* name) const
{
    if (m_node) {
        pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node);
        pugi::xml_node s = name ? n.previous_sibling(name) : n.previous_sibling();
        if (!name) {
            while (s && s.type() != pugi::node_element) s = s.previous_sibling();
        }
        if (s && s.type() == pugi::node_element) {
            return XmlDomElement(m_xml, pack_handle(s));
        }
    }
    return XmlDomElement(m_xml, xml_node_handle());
}

XmlDomElement XmlDomNode::toElement() const
{
    if (m_node) {
        pugi::xml_node n = unpack_handle<pugi::xml_node>(m_node);
        if (n.type() == pugi::node_element) {
            return XmlDomElement(m_xml, pack_handle(n));
        }
    }
    return XmlDomElement(m_xml, xml_node_handle());
}

// ================================================
// XmlDomAttribute
// ================================================

XmlDomAttribute::XmlDomAttribute(const std::shared_ptr<XmlDomImplData>& data, xml_attr_handle attribute)
    : m_xml(data), m_attribute(attribute)
{
}

bool XmlDomAttribute::isNull() const
{
    return !static_cast<bool>(m_attribute);
}

String XmlDomAttribute::attributeName() const
{
    if (!m_attribute) {
        return String();
    }
    pugi::xml_attribute a = unpack_handle<pugi::xml_attribute>(m_attribute);
    return String::fromUtf8(a.name());
}

String XmlDomAttribute::value() const
{
    if (!m_attribute) {
        return String();
    }
    pugi::xml_attribute a = unpack_handle<pugi::xml_attribute>(m_attribute);
    return String::fromUtf8(a.value());
}

XmlDomAttribute XmlDomAttribute::nextAttribute() const
{
    if (!m_attribute) {
        return XmlDomAttribute(m_xml, xml_attr_handle());
    }
    pugi::xml_attribute a = unpack_handle<pugi::xml_attribute>(m_attribute).next_attribute();
    return XmlDomAttribute(m_xml, pack_handle(a));
}

// ================================================
// XmlDomElement
// ================================================

XmlDomElement::XmlDomElement(const std::shared_ptr<XmlDomImplData>& data, xml_node_handle node)
    : XmlDomNode(data, node)
{
}

String XmlDomElement::text() const
{
    auto e = unpack_handle<pugi::xml_node>(m_node);
    if (!e || e.type() != pugi::node_element) {
        return String();
    }
    String result;
    for (pugi::xml_node c = e.first_child(); c; c = c.next_sibling()) {
        const pugi::xml_node_type t = c.type();
        if (t == pugi::node_pcdata || t == pugi::node_cdata) {
            result += String::fromUtf8(c.value());
        }
    }
    return result;
}

XmlDomAttribute XmlDomElement::firstAttribute() const
{
    if (m_node) {
        auto e = unpack_handle<pugi::xml_node>(m_node);
        if (e && e.type() == pugi::node_element) {
            return XmlDomAttribute(m_xml, pack_handle(e.first_attribute()));
        }
    }
    return XmlDomAttribute(m_xml, xml_attr_handle());
}

XmlDomAttribute XmlDomElement::attribute(const char* name) const
{
    if (m_node) {
        auto e = unpack_handle<pugi::xml_node>(m_node);
        if (e && e.type() == pugi::node_element) {
            return XmlDomAttribute(m_xml, pack_handle(e.attribute(name)));
        }
    }
    return XmlDomAttribute(m_xml, xml_attr_handle());
}

// ================================================
// XmlDomDocument
// ================================================

XmlDomDocument::XmlDomDocument()
{
    m_xml = std::make_shared<XmlDomImplData>();
}

void XmlDomDocument::setContent(const ByteArray& data)
{
    m_xml->doc.reset();
    m_xml->result = m_xml->doc.load_buffer(data.constData(), data.size());
    m_xml->triedload = true;

    if (m_xml->result.status != pugi::status_ok) {
        LOGE() << errorString();
    }
}

XmlDomElement XmlDomDocument::rootElement() const
{
    pugi::xml_node e = m_xml->doc.document_element();
    return XmlDomElement(m_xml, pack_handle(e));
}

bool XmlDomDocument::hasError() const
{
    return m_xml->triedload && m_xml->result.status != pugi::status_ok;
}

String XmlDomDocument::errorString() const
{
    if (m_xml->triedload) {
        return String::fromUtf8(m_xml->result.description());
    }
    return String();
}
