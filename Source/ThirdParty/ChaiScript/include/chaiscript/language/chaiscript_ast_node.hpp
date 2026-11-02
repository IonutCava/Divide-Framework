// This file is distributed under the BSD License.
// See "license.txt" for details.
// Copyright 2009-2012, Jonathan Turner (jonathan@emptycrate.com)
// Copyright 2009-2018, Jason Turner (jason@emptycrate.com)
// http://www.chaiscript.com

// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#ifndef CHAISCRIPT_AST_NODE_HPP_
#define CHAISCRIPT_AST_NODE_HPP_

#include "../dispatchkit/dispatchkit.hpp"

namespace chaiscript {
   namespace detail{
    class Dispatch_State;
   };

  /// Types of AST nodes available to the parser and eval
  enum class AST_Node_Type {
    Id,
    Fun_Call,
    Unused_Return_Fun_Call,
    Arg_List,
    Equation,
    Var_Decl,
    Assign_Decl,
    Array_Call,
    Dot_Access,
    Lambda,
    Block,
    Scopeless_Block,
    Def,
    While,
    If,
    For,
    Ranged_For,
    Inline_Array,
    Inline_Map,
    Return,
    File,
    Prefix,
    Break,
    Continue,
    Map_Pair,
    Value_Range,
    Inline_Range,
    Try,
    Catch,
    Finally,
    Method,
    Attr_Decl,
    Logical_And,
    Logical_Or,
    Reference,
    Switch,
    Case,
    Default,
    Noop,
    Class,
    Binary,
    Arg,
    Global_Decl,
    Constant,
    Compiled
  };

  
  namespace {
    /// Helper lookup to get the name of each node type
    constexpr const char *ast_node_type_to_string(AST_Node_Type ast_node_type) noexcept {
      constexpr const char *const ast_node_types[] = {"Id", "Fun_Call", "Unused_Return_Fun_Call", "Arg_List", "Equation", "Var_Decl", "Assign_Decl", "Array_Call", "Dot_Access", "Lambda", "Block", "Scopeless_Block", "Def", "While", "If", "For", "Ranged_For", "Inline_Array", "Inline_Map", "Return", "File", "Prefix", "Break", "Continue", "Map_Pair", "Value_Range", "Inline_Range", "Try", "Catch", "Finally", "Method", "Attr_Decl", "Logical_And", "Logical_Or", "Reference", "Switch", "Case", "Default", "Noop", "Class", "Binary", "Arg", "Global_Decl", "Constant", "Compiled"};

      return ast_node_types[static_cast<int>(ast_node_type)];
    }
  } // namespace
  
  /// \brief Convenience type for file positions
  struct File_Position {
    int line = 0;
    int column = 0;

    constexpr File_Position(int t_file_line, int t_file_column) noexcept
        : line(t_file_line)
        , column(t_file_column) {
    }

    constexpr File_Position() noexcept = default;
  };

  struct Parse_Location {
    Parse_Location(std::string t_fname = "", const int t_start_line = 0, const int t_start_col = 0, const int t_end_line = 0, const int t_end_col = 0)
        : start(t_start_line, t_start_col)
        , end(t_end_line, t_end_col)
        , filename(std::make_shared<std::string>(std::move(t_fname))) {
    }

    Parse_Location(std::shared_ptr<std::string> t_fname,
                   const int t_start_line = 0,
                   const int t_start_col = 0,
                   const int t_end_line = 0,
                   const int t_end_col = 0)
        : start(t_start_line, t_start_col)
        , end(t_end_line, t_end_col)
        , filename(std::move(t_fname)) {
    }

    File_Position start;
    File_Position end;
    std::shared_ptr<std::string> filename;
  };

  /// \brief Struct that doubles as both a parser ast_node and an AST node.
  struct AST_Node {
  public:
    const AST_Node_Type identifier;
    const std::string text;
    Parse_Location location;

    const std::string &filename() const noexcept { return *location.filename; }

    const File_Position &start() const noexcept { return location.start; }

    const File_Position &end() const noexcept { return location.end; }

    std::string pretty_print() const {
      std::ostringstream oss;

      oss << text;

      for (auto &elem : get_children()) {
        oss << elem.get().pretty_print() << ' ';
      }

      return oss.str();
    }

    virtual std::vector<std::reference_wrapper<AST_Node>> get_children() const = 0;
    virtual Boxed_Value eval(const chaiscript::detail::Dispatch_State &t_e) const = 0;

    /// Prints the contents of an AST node, including its children, recursively
    std::string to_string(const std::string &t_prepend = "") const {
      std::ostringstream oss;

      oss << t_prepend << "(" << ast_node_type_to_string(this->identifier) << ") " << this->text << " : " << this->location.start.line
          << ", " << this->location.start.column << '\n';

      for (auto &elem : get_children()) {
        oss << elem.get().to_string(t_prepend + "  ");
      }
      return oss.str();
    }

    static inline bool get_bool_condition(const Boxed_Value &t_bv, const chaiscript::detail::Dispatch_State &t_ss);

    virtual ~AST_Node() noexcept = default;
    AST_Node(AST_Node &&) = default;
    AST_Node &operator=(AST_Node &&) = delete;
    AST_Node(const AST_Node &) = delete;
    AST_Node &operator=(const AST_Node &) = delete;

  protected:
    AST_Node(std::string t_ast_node_text, AST_Node_Type t_id, Parse_Location t_loc)
        : identifier(t_id)
        , text(std::move(t_ast_node_text))
        , location(std::move(t_loc)) {
    }
  };

  /// \brief Typedef for pointers to AST_Node objects. Used in building of the AST_Node tree
  using AST_NodePtr = std::unique_ptr<AST_Node>;
  using AST_NodePtr_Const = std::unique_ptr<const AST_Node>;

  struct AST_Node_Trace {
    const AST_Node_Type identifier;
    const std::string text;
    Parse_Location location;

    const std::string &filename() const noexcept { return *location.filename; }

    const File_Position &start() const noexcept { return location.start; }

    const File_Position &end() const noexcept { return location.end; }

    std::string pretty_print() const {
      std::ostringstream oss;

      oss << text;

      for (const auto &elem : children) {
        oss << elem.pretty_print() << ' ';
      }

      return oss.str();
    }

    std::vector<AST_Node_Trace> get_children(const AST_Node &node) {
      const auto node_children = node.get_children();
      return std::vector<AST_Node_Trace>(node_children.begin(), node_children.end());
    }

    AST_Node_Trace(const AST_Node &node)
        : identifier(node.identifier)
        , text(node.text)
        , location(node.location)
        , children(get_children(node)) {
    }

    std::vector<AST_Node_Trace> children;
  };
} // namespace chaiscript

#endif /* CHAISCRIPT_AST_NODE_HPP_ */