//
// modelfileloader.cpp
// Created by James Barbetti on 01-Apr-2021.
//

#include "modelfileloader.h"
#include "modelexpression.h" //for ModelExpression::ModelException
#include <utils/stringfunctions.h>
#include <tree/phylotree.h> //for TREE_LOG_LINE macro
   

typedef ModelExpression::InterpretedExpression Interpreter;

ModelFileLoader::ModelFileLoader(const char* path): file_path(path) {
}
        
std::string ModelFileLoader::stringScalar(const YAML::Node& node,
                                          const char* key,
                                          const char* default_value) {
    auto   scalar_node = node[key];
    return scalar_node ? scalar_node.Scalar() : default_value;
}
    
bool ModelFileLoader::booleanScalar(const YAML::Node& node,
                                    const char* key,
                                    const bool default_value) {
    std::string s = string_to_lower(stringScalar(node, key, ""));
    if (s.empty()) {
        return default_value;
    }
    return s == "true" || s == "yes" || s == "t" || s == "y" || s == "1";
}

int ModelFileLoader::integerScalar(const YAML::Node& node,
                                   const char* key,
                                   const int default_value) {
    std::string s = stringScalar(node, key, "");
    if (s.empty()) {
        return default_value;
    }
    if (s[0]<'0' || '9'<s[0]) {
        return default_value;
    }
    return atoi(s.c_str());
}

void ModelFileLoader::complainIfSo(bool        check_me,
                                   std::string error_message) {
    if (check_me) {
        outError(error_message);
    }
}

void ModelFileLoader::complainIfNot(bool check_me,
                          std::string error_message) {
    if (!check_me) {
        outError(error_message);
    }
}
    
double ModelFileLoader::toDouble(const YAML::Node& i, double default_val) {
    if (!i.IsScalar()) {
        return default_val;
    }
    std::string double_string = i.Scalar();
    return convert_double_nothrow(double_string.c_str(), default_val);
}
    
ModelParameterRange ModelFileLoader::parseRange(const YAML::Node& node,
                                                const char* key,
                                                const ModelParameterRange& default_value ) {
    auto r = node[key];
    if (!r) {
        return default_value;
    }
    ModelParameterRange range;
    //Todo: what if range is a string?
    if (r.IsSequence()) {
        int ix = 0;
        for (auto i : r) {
            if (ix==0) {
                range.first  = toDouble(i, 0);
            } else if (ix==1) {
                range.second = toDouble(i, 0);
            } else {
                throw ModelExpression::ModelException
                      ("Range may only have two bounds (lower, upper)");
            }
            ++ix;
        }
        range.is_set = ( 1 <= ix );
        if ( ix == 1 ) {
            range.second = range.first;
        } else if (range.second < range.first) {
            std::stringstream complaint;
            complaint << "Range has lower bound (" << range.first << ")"
                      << " greater than its upper bound (" << range.second << ")";
            throw ModelExpression::ModelException(complaint.str());
        }
    }
    return range;
}
    
void ModelFileLoader::parseYAMLModelParameters(const YAML::Node& params,
                                               ModelInfoFromYAMLFile& info,
                                               PhyloTree* report_to_tree) {
    //
    //Assumes: params is a sequence of parameter declarations
    //
    for (const YAML::Node& param: params) {
        const YAML::Node& name_node = param["name"];
        if (name_node) {
            if (name_node.IsScalar()) {
                parseModelParameter(param, name_node.Scalar(), info,
                                    report_to_tree);
            }
            else if (name_node.IsSequence()) {
                for (auto current_name: name_node) {
                    if (current_name.IsScalar()) {
                        std::string name = current_name.Scalar();
                        parseModelParameter(param, name, info,
                                            report_to_tree);
                    }
                }
            }
            else {
                outError("Model parameter must have a name");
            }
        }
    }
}

void ModelFileLoader::parseModelParameter(const YAML::Node& param,
                                          std::string name,
                                          ModelInfoFromYAMLFile& info,
                                          PhyloTree* report_to_tree) {
    YAMLFileParameter p;
    p.name       = name;
    auto bracket = p.name.find('(');
    if ( bracket != std::string::npos ) {
        p.is_subscripted = true;
        const char* range = p.name.c_str() + bracket + 1;
        int ix;
        p.minimum_subscript = convert_int(range, ix);
        if (strncmp(range + ix, "..", 2)==0) {
            range = range + ix + 2;
            p.maximum_subscript = convert_int(range, ix);
        } else {
            p.maximum_subscript = p.minimum_subscript;
            p.minimum_subscript   = 1;
        }
        if (strncmp(range + ix, ")", 1)!=0) {
            const char* msg = "Subscript range does not end with right parenthesis";
            throw ModelExpression::ModelException(msg);
        }
        p.name = p.name.substr(0, bracket);
    } else {
        p.is_subscripted    = false;
        p.minimum_subscript = 0;
        p.maximum_subscript = 1;
    }
    
    p.type_name = string_to_lower(stringScalar(param, "type", p.type_name.c_str()));
    if (p.type_name=="matrix") {
        complainIfSo(p.is_subscripted,
                     "Matrix subscripts are implied by the matrix value itself, "
                     " but " + p.name + " parameter of model " + info.getName() +
                     " was explicitly subscripted (which is not supported).");
        auto value   = param["value"];
        auto formula = param["formula"];
        auto rank    = param["rank"];
        complainIfNot(value || (formula && rank), p.name +
                      " matrix parameter's value must be defined"
                      " in model " + info.getName() + ".");
        parseMatrixParameter(param, name, info, report_to_tree);
        return;
    }
    
    bool overriding = false;
    for (const YAMLFileParameter& oldp: info.parameters) {
        if (oldp.name == p.name) {
            complainIfNot(oldp.is_subscripted == p.is_subscripted,
                          "Canot redefine subscripted parameter"
                          " as unsubscripted (or vice versa)");
            complainIfNot(oldp.minimum_subscript == p.minimum_subscript,
                          "Cannot redefine parameter subscript range");
            complainIfNot(oldp.maximum_subscript == p.maximum_subscript,
                          "Cannot redefine parameter subscript range");
            p = oldp;
            overriding = true;
            break;
        }
    }
    
    if (p.type_name=="rate") {
        p.type = ModelParameterType::RATE;
    } else if (p.type_name=="frequency") {
        p.type = ModelParameterType::FREQUENCY;
    } else if (p.type_name=="weight") {
        p.type = ModelParameterType::WEIGHT;
    } else {
        p.type = ModelParameterType::OTHER;
    }
    
    auto count = p.maximum_subscript - p.minimum_subscript + 1;
    ASSERT(0<count);
    double dv   = 0.0; //default initial value
    if (p.type==ModelParameterType::FREQUENCY) {
        dv = 1.0 / (double)count;
                    //Todo: should be 1.0 divided by number of states
                    //determined from the data type (info.data_type_name ?)
                    //Or 1 divided by the number of parameters.
    } else if (p.type==ModelParameterType::RATE) {
        dv = 1.0;
    } else if (p.type==ModelParameterType::WEIGHT) {
        dv = 1.0 / (double)count;    //Todo: Should be 1.0 divided by # of parameters
    }
    //Todo: What if name was a list, and initValue is also a list?!
    std::string value_string = stringScalar(param, "initValue", "");
    p.range                  = parseRange  (param, "range", p.range);
    if (value_string!="") {
        p.value = convert_double_nothrow(value_string.c_str(), dv);
    } else if (!overriding) {
        if (p.range.is_set) {
            //Initial value has to be inside the legal range.
            if (dv<p.range.first) {
                dv = p.range.first;
            } if (p.range.second<dv) {
                dv = p.range.second;
            }
        }
        p.value = dv;
    }
    p.description = stringScalar(param, "description", p.description.c_str());
    std::stringstream msg;
    msg  << "Parsed parameter " << p.name
         << " of type " << p.type_name
         << ", with range " << p.range.first
         << " to " << p.range.second
         << ", and initial value " << p.value;
    TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity, msg.str());
    info.addParameter(p);
}

void ModelFileLoader::parseMatrixParameter(const YAML::Node& param,
                                           std::string name,
                                           ModelInfoFromYAMLFile& info,
                                           PhyloTree* report_to_tree) {
    //Assumed: parameter has a "value" entry, and it is a sequence of sequences
    
    auto         value        = param["value"];
    int          column_count = 0;
    StringMatrix expressions;
    
    auto         formula_node = param["formula"];
    std::string  formula;

    auto         rank_node    = param["rank"];
    int          rank         = 0;

    if (value) {
        complainIfNot(value.IsSequence(),
                      "value of " + name +
                      " matrix of model " + info.getName() +
                      " was not a matrix");
        for (auto row : value) {
            ++rank;
            std::stringstream s;
            s << "Row " << rank << " of " + name + " matrix "
              << " for model " << info.model_name
              << " in " << info.model_file_path;
            std::string context = s.str();
            complainIfNot(row.IsSequence(),
                          context + " is not a sequence" );
            StrVector expr_row;
            for (auto col : row) {
                if (col.IsNull()) {
                    expr_row.emplace_back("");
                }
                else if (!col.IsScalar()) {
                    std::stringstream s2;
                    s2 << "Column " << (expr_row.size()+1)
                       << " of " << context << " is not a scalar";
                    outError(s2.str());
                } else {
                    expr_row.emplace_back(col.Scalar());
                }
            }
            expressions.emplace_back(expr_row);
            column_count = ( expr_row.size() < column_count )
                         ? column_count : expr_row.size();
        }
        expressions.makeRectangular(column_count);
    }
    else if (!formula_node || !rank_node) {
        outError(name +
                 " matrix of model " + info.getName() +
                 " had no value, and lacked either a rank or a formula");
    }
    if (rank_node) {
        complainIfNot(rank_node.IsScalar(), "rank of " + name +
                      " matrix of model " + info.getName() +
                      " was not a scalar");
        std::string rank_str = rank_node.Scalar();
        info.forceAssign("num_states", (double) info.num_states);
        
        Interpreter interpreter(info, rank_str);
        double rank_dbl = interpreter.evaluate();
        rank            = (int)floor(rank_dbl);
        complainIfNot(0<rank, "rank of " + name + " matrix of model "
                      + info.getName() + " was invalid (" + rank_str + ")");
        TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity,
                      "Rank of " << info.getName() << "." << name <<
                      " was " << rank_str << " ... or " << rank);
    }
    if (formula_node) {
        complainIfNot(formula_node.IsScalar(), "formula of " + name +
                      " matrix of model " + info.getName() +
                      " was not a scalar");
        formula = formula_node.Scalar();
    }
    
    std::string lower_name = string_to_lower(name);
    if (lower_name=="ratematrix") {
        info.rate_matrix_rank           = rank;
        info.rate_matrix_expressions    = expressions;
        info.rate_matrix_formula        = formula;
    }
    else if (lower_name=="tiplikelihood") {
        info.tip_likelihood_rank        = rank;
        info.tip_likelihood_expressions = expressions;
        info.tip_likelihood_formula     = formula;
    }
    else {
        outError(name + " matrix parameter not recognized"
                 " in " + info.getName() + " model");
    }
    std::stringstream matrix_stream;
    dumpMatrixTo(lower_name.c_str(), info, expressions, rank,
                 formula, matrix_stream);
    TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity, matrix_stream.str());
}

YAMLFileParameter
    ModelFileLoader::addDummyFrequencyParameterTo(ModelInfoFromYAMLFile& info,
                                                  PhyloTree* report_to_tree) {
    YAMLFileParameter   p;
    p.name              = "freq";
    p.is_subscripted    = true;
    p.minimum_subscript = 1;
    p.maximum_subscript = info.getNumStates();
    p.type_name         = "frequency";
    p.type              = FREQUENCY;
    p.value             = 1 / (double) info.getNumStates();
    info.addParameter(p);
    return p;
}

void ModelFileLoader::parseYAMLMixtureModels(const YAML::Node& mixture_models,
                                             ModelInfoFromYAMLFile& info,
                                             ModelListFromYAMLFile& list,
                                             PhyloTree* report_to_tree) {
    TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity, "Processing mixtures" );
    info.mixed_models = new MapOfModels();
    for (const YAML::Node& model: mixture_models) {
        std::string child_model_name = stringScalar(model, "substitutionmodel", "");
        TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity, "Processing mixture model" );
        ModelInfoFromYAMLFile child_info;
        parseYAMLSubstitutionModel(model, child_model_name, child_info,
                                   list, &info, report_to_tree);
        (*info.mixed_models)[info.getName()] = child_info;
    }
}


void ModelFileLoader::parseYAMLModelConstraints(const YAML::Node& constraints,
                                                ModelInfoFromYAMLFile& info,
                                                PhyloTree* report_to_tree) {
    for (const YAML::Node& constraint: constraints) {
        //constraints are assignments of the form: name = value
        //and are equivalent to parameter name/initialValue pairs
        //Todo: For now, I don't want to support (x,y) = (1,2).
        //
        std::stringstream complaint;
        if (!constraint.IsScalar()) {
            complaint << "Constraint setting"
                      << " for model " << info.model_name
                      << " was not a scalar.";
            outError(complaint.str());
        }
        std::string constraint_string = constraint.Scalar();
        ModelExpression::InterpretedExpression interpreter(info, constraint_string);
        ModelExpression::Expression* x = interpreter.expression();
        
        if (!x->isAssignment()) {
            complaint << "Constraint setting for model " << info.model_name
            << " was not an asignment: " << constraint_string;
            outError(complaint.str());
        }
        ModelExpression::Assignment* a = dynamic_cast<ModelExpression::Assignment*>(x);
        
        if (!a->getTarget()->isVariable()) {
            delete x;
            complaint << "Constraint setting for model " << info.model_name
                      << " did not assign a variable: " << constraint_string;
            outError(complaint.str());
        }
        ModelExpression::Variable* v = a->getTargetVariable();
        double setting = a->getExpression()->evaluate();
        ModelVariable& mv = info.assign(v->getName(), setting);
        mv.markAsFixed();
        TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity,
                      "Assigned " << v->getName()
                      << " := " << setting);
    }
}

void ModelFileLoader::parseRateMatrix(const YAML::Node& rate_matrix,
                                      ModelInfoFromYAMLFile& info,
                                      PhyloTree* report_to_tree) {
    //Assumes rate_matrix is a sequence (of rows)
    size_t column_count = 0;
    for (auto row : rate_matrix) {
        ++info.rate_matrix_rank;
        std::stringstream s;
        s << "Row " << info.rate_matrix_rank << " of rate matrix "
          << " for model " << info.model_name
          << " in " << info.model_file_path;
        std::string context = s.str();
        complainIfNot(row.IsSequence(),
                      context + " is not a sequence" );
        StrVector expr_row;
        for (auto col : row) {
            if (col.IsNull()) {
                expr_row.emplace_back("");
            }
            else if (!col.IsScalar()) {
                std::stringstream s2;
                s2 << "Column " << (expr_row.size()+1)
                   << " of " << context << " is not a scalar";
                outError(s2.str());
            } else {
                expr_row.emplace_back(col.Scalar());
            }
        }
        info.rate_matrix_expressions.emplace_back(expr_row);
        column_count = ( expr_row.size() < column_count )
                     ? column_count : expr_row.size();
    }
    size_t row_count = info.rate_matrix_expressions.size();
    if ( row_count != column_count) {
        std::stringstream s2;
        s2 << "Rate matrix "
           << " for model " << info.model_name
           << " in " << info.model_file_path
           << " was not square: it had " << row_count << " rows"
           << " and " << column_count << " columns.";
        outError(s2.str());
    }
    info.rate_matrix_expressions.makeRectangular(column_count);
    
    //
    //Todo: Are off-diagonal entries in the matrix allowed
    //to be blank?
    //
    std::stringstream matrix_stream;
    dumpMatrixTo("rate", info, info.rate_matrix_expressions,
                 info.rate_matrix_rank, info.rate_matrix_formula,
                 matrix_stream);
    TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity, matrix_stream.str());
}

void ModelFileLoader::dumpMatrixTo(const char* name, ModelInfoFromYAMLFile& info,
                                   const StringMatrix& matrix, int rank,
                                   const std::string& formula, std::stringstream &out) {
    info.forceAssign("num_states", (double)info.num_states);
    ModelVariable& row_var    = info.forceAssign("row",    (double)0);
    ModelVariable& column_var = info.forceAssign("column", (double)0);

    std::string with_formula;
    std::stringstream dump;
    if (!matrix.empty()) {
        //If there's a matrix of expressions, dump the expressions
        int row = 0;
        for (auto r : matrix) {
            row_var.setValue(row);
            const char* separator = "";
            int col = 0;
            for (auto c: r) {
                column_var.setValue(col);
                dump << separator << c;
                separator = " : ";
                ++col;
            }
            dump << "\n";
            ++row;
        }
    }
    else {
        //If there is a formula, dump the formula, and its
        //current value, for each entry in the matrix
        with_formula = " (with formula " + formula + ")";
        for (int row=0; row<rank; ++row) {
            row_var.setValue(row);
            const char* separator = "";
            for (int col=0; col<rank; ++col) {
                column_var.setValue(col);
                try {
                    Interpreter interpreter(info, formula);
                    double value = interpreter.evaluate();
                    dump << separator << value;
                }
                catch (ModelExpression::ModelException& x) {
                    dump << separator << " ERROR";
                }
                separator = " : ";
            }
            dump << "\n";
        }
    }
    out << name << " matrix for " << info.model_name
        << with_formula
        << " is...\n" << dump.str();
}

void ModelFileLoader::parseYAMLSubstitutionModel(const YAML::Node& substitution_model,
                                                 const std::string& name_of_model,
                                                 ModelInfoFromYAMLFile& info,
                                                 ModelListFromYAMLFile& list,
                                                 ModelInfoFromYAMLFile* parent_model,
                                                 PhyloTree* report_to_tree) {
    
    std::string superclass_model_name = stringScalar(substitution_model, "frommodel", "");
    if (superclass_model_name != "") {
        if (list.hasModel(superclass_model_name)) {
            info = list.getModel(superclass_model_name);
            TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity,
                          "Model " << name_of_model
                          << " is based on model " << superclass_model_name);
        } else {
            std::stringstream complaint;
            complaint << "Model " << name_of_model << " specifies frommodel "
                      << superclass_model_name << ", but that model was not found.";
            outError(complaint.str());
        }
    }
    
    info.model_file_path = file_path;
    info.model_name      = name_of_model.empty() ? superclass_model_name : name_of_model;
    info.citation        = stringScalar(substitution_model,  "citation",   info.citation.c_str());
    info.DOI             = stringScalar(substitution_model,  "doi",        info.DOI.c_str());
    info.reversible      = booleanScalar(substitution_model, "reversible", info.reversible);
    info.data_type_name  = stringScalar(substitution_model,  "datatype",   info.data_type_name.c_str());
    //Note: doco currently says this will be called "forData".
    //
    //Todo: read off the in-lined datatype (if there is one).
    //
    
    info.num_states      = integerScalar(substitution_model, "numStates", 0);
    if (info.num_states==0) {
        info.num_states = 4;
    }
    
    //
    //Todo: extract other information from the subsstitution model.
    //      Such as parameters and rate matrices and so forth
    //
    auto params = substitution_model["parameters"];
    if (params) {
        complainIfNot(params.IsSequence(),
                      "Parameters of model " + model_name +
                      " in file " + file_path + " not a sequence");
        parseYAMLModelParameters(params, info, report_to_tree);
    }

    //Mixtures have to be handled before constraints, as constraints
    //that are setting parameters in mixed models... would otherwise
    //not be resolved correctly.
    auto mixtures = substitution_model["mixture"];
    if (mixtures) {
        complainIfNot(mixtures.IsSequence(),
                      "Constraints for model " + model_name +
                      " in file " + file_path + " not a sequence");
        parseYAMLMixtureModels(mixtures, info, list, report_to_tree);
    }
    
    auto constraints = substitution_model["constraints"];
    if (constraints) {
        complainIfNot(constraints.IsSequence(),
                      "Constraints for model " + model_name +
                      " in file " + file_path + " not a sequence");
        parseYAMLModelConstraints(constraints, info, report_to_tree);
    }
    
    //
    //Note: if rateMatrix was read as a parameter,
    //      rate_matrix_expressions will aready have been set.
    //
    auto rateMatrix = substitution_model["rateMatrix"];
    if (info.rate_matrix_expressions.empty() && !mixtures) {
        complainIfNot(rateMatrix, "Model " + model_name +
                      " in file " + file_path +
                      " does not specify a rateMatrix" );
    }
    
    //If this model subclasses another it doesn't have to specify
    //a rate matrix (if it doesn't it inherits from its parent model).
    if (rateMatrix) {
        parseRateMatrix(rateMatrix, info, report_to_tree);
    }
    
    auto stateFrequency = substitution_model["stateFrequency"];
    if (stateFrequency) {
        //
        //Check that dimension of the specified parameter is the
        //same as the rank of the rate matrix (it must be!).
        //
        std::string freq     = stateFrequency.IsScalar() ? stateFrequency.Scalar() : "";
        std::string low_freq = string_to_lower(freq);
        if (low_freq=="estimate") {
            info.frequency_type = StateFreqType::FREQ_ESTIMATE;
        } else if (low_freq=="empirical") {
            info.frequency_type = StateFreqType::FREQ_EMPIRICAL;
        } else if (low_freq=="uniform") {
            info.frequency_type = StateFreqType::FREQ_EQUAL;
        } else if (info.isFrequencyParameter(low_freq)) {
            info.frequency_type = StateFreqType::FREQ_USER_DEFINED;
        } else if (stateFrequency.IsSequence()) {
            info.frequency_type = StateFreqType::FREQ_USER_DEFINED;
            YAMLFileParameter freq_param = addDummyFrequencyParameterTo(info, report_to_tree);
            int subscript = freq_param.minimum_subscript;
            for (auto f: stateFrequency) {
                complainIfNot(f.IsScalar(), "Model " + model_name +
                              " in file " + file_path +
                              " has unrecognized frequency ");
                complainIfNot(subscript<=freq_param.maximum_subscript,
                              "Too many frequencies specified for "
                              "Model " + model_name +
                              " in file " + file_path);
                ModelExpression::InterpretedExpression x(info, f.Scalar());
                auto   var_name  = freq_param.getSubscriptedVariableName(subscript);
                double var_value = x.evaluate();
                info.assign(var_name, var_value);
                TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity,
                              "Assigned frequency: " << var_name
                              << " := " << var_value  );
                ++subscript;
            }
        }
    } else {
        //If we have parameters with a type of frequency, we're all good.
        //If we don't, then what?   We might have inherited from a parent
        //model, too.  That'd be okay.
    }
    
    const char* recognized_string_property_names[] = {
        "errormodel", //One of "+E", "+EA", "+EC", "+EG", "+ET"
                      //recognized by ModelDNAError.
    };
    for (const char* prop_name : recognized_string_property_names ) {
        auto prop_node = substitution_model[prop_name];
        if (prop_node) {
            if (prop_node.IsScalar()) {
                std::string prop_value = prop_node.Scalar();
                info.string_properties[prop_name] = prop_value;
                TREE_LOG_LINE(*report_to_tree, YAMLModelVerbosity,
                              "string property " << prop_name <<
                              " set to " << prop_value);
            }
            //Todo: what about lists?
        }
    }

    auto weight = substitution_model["weight"];
    if (weight) {
        complainIfNot(parent_model!=nullptr,
                      "Model " + model_name +
                      " in file " + file_path +
                      " is not part of a mixture model" );
        //Todo: decide what to do with weight ; it may well
        //      be a reference to variable in the parent
        //      mixture model.  Does it have to be?
    }

    auto scale  = substitution_model["scale"];
    if (scale) {
        //Todo: can you legitimately set the scale
        //      for a substitution model that is not part
        //      of a mixture model.
        //
        complainIfNot(parent_model!=nullptr,
                      "Model " + model_name +
                      " in file " + file_path +
                      " is not part of a mixture model" );
        //Todo: set the scale.
    }
    
    if (parent_model!=nullptr) {
        if (!scale) {
            //Default the scale
        }
        complainIfNot(weight,
                      "No weight specified"
                      " for model " + model_name +
                      " in mixture " + parent_model->getName() +
                      " in file " + file_path);
    }
}