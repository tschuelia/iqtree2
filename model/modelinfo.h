//
//  modelinfo.h
//  Created by James Barbetti on 28-Jan-2021
//

#ifndef modelinfo_h
#define modelinfo_h

#include <string>
#include <utils/tools.h> //for ASCType

class ModelMarkov;
class PhyloTree;
class ModelFileLoader;

class ModelInfo {
public:
    ModelInfo()                     = default;
    ModelInfo(const ModelInfo& rhs) = default;
    virtual ~ModelInfo()            = default;
    
    virtual std::string getFreeRateParameters(int& num_rate_cats,
                                              bool &fused_mix_rate) const = 0;
    virtual std::string getFrequencyMixtureParams(std::string& freq_str) const  = 0;
    virtual void getFrequencyOptions(std::string& freq_str,
                                     StateFreqType& freq_type,
                                     std::string& freq_params,
                                     bool& optimize_mixmodel_weight) const = 0;

    virtual void   getGammaParameters(int& num_rate_cats,
                                      double& gamma_shape) const = 0;
    virtual std::string getHeterotachyParameters(bool is_mixture_model,
                                                 int& num_rate_cats,
                                                 bool& fused_mix_rate) const = 0;
    virtual double getProportionOfInvariantSites() const = 0;
    
    virtual bool hasAscertainmentBiasCorrection()  const = 0;
    virtual bool hasRateHeterotachy()              const = 0;
    
    virtual bool isFreeRate()                      const = 0;
    virtual bool isFrequencyMixture()              const = 0;
    virtual bool isGammaModel()                    const = 0;
    virtual bool isInvariantModel()                const = 0;
    virtual bool isMixtureModel()                  const = 0;
    virtual bool isModelFinder()                   const = 0;
    virtual bool isModelFinderOnly()               const = 0;
    virtual bool isPolymorphismAware()             const = 0;
    virtual bool isWeissAndVonHaeselerTest()       const = 0;
    
    virtual ASCType     extractASCType(std::string& leftover_name) const = 0;
    virtual std::string extractMixtureModelList(std::string& leftover_name) const = 0;
    virtual std::string extractPolymorphicHeterozygosity(std::string& leftover_name) const = 0;
    virtual void        updateName(const std::string& name) = 0;
};

class ModelInfoFromName: public ModelInfo {
private:
    std::string model_name;
public:
    friend class ModelFileLoader;
    
    explicit ModelInfoFromName(std::string name);
    explicit ModelInfoFromName(const char* name);
    virtual ~ModelInfoFromName() = default;
    
    virtual std::string getFreeRateParameters(int& num_rate_cats,
                                              bool &fused_mix_rate) const;
    virtual std::string getFrequencyMixtureParams(std::string& freq_str) const;
    virtual void        getFrequencyOptions(std::string& freq_str,
                                            StateFreqType& freq_type,
                                            std::string& freq_params,
                                            bool& optimize_mixmodel_weight) const;
    virtual void        getGammaParameters(int& num_rate_cats,
                                           double& gamma_shape) const;
    virtual std::string getHeterotachyParameters(bool is_mixture_model,
                                                 int& num_rate_cats,
                                                 bool& fused_mix_rate) const;
    virtual double getProportionOfInvariantSites() const;

    virtual bool hasAscertainmentBiasCorrection()  const;
    virtual bool hasRateHeterotachy()              const;
    
    virtual bool isFreeRate()                      const;
    virtual bool isFrequencyMixture()              const;
    virtual bool isGammaModel()                    const;
    virtual bool isInvariantModel()                const;
    virtual bool isMixtureModel()                  const;
    virtual bool isModelFinder()                   const;
    virtual bool isModelFinderOnly()               const;
    virtual bool isPolymorphismAware()             const;
    virtual bool isWeissAndVonHaeselerTest()       const;

    ASCType     extractASCType(std::string& leftover_name) const;
    std::string extractMixtureModelList(std::string& leftover_name) const;
    std::string extractPolymorphicHeterozygosity(std::string& leftover_name) const;
    void        updateName(const std::string& name);
};

class ModelParameterRange: public std::pair<double,double> {
public:
    typedef std::pair<double,double> super;
    bool is_set;
    ModelParameterRange(): super(0,0), is_set(false) {}
};

enum ModelParameterType {
    RATE, FREQUENCY, WEIGHT, OTHER
};

class YAMLFileParameter {
public:
    std::string         name;
    std::string         description;
    bool                is_subscripted;
    int                 minimum_subscript;
    int                 maximum_subscript;
    std::string         type_name;
    ModelParameterType  type;
    ModelParameterRange range;
    double              value;
    YAMLFileParameter();
    std::string getSubscriptedVariableName(int subscript) const;
};


class ModelVariable {
public:
    ModelParameterRange range;
    ModelParameterType  type;
    double              value;
    bool                is_fixed;
    ModelVariable();
    ModelVariable(ModelParameterType t, const ModelParameterRange& r, double v);
    ~ModelVariable() = default;
    ModelVariable(const ModelVariable& rhs) = default;
    ModelVariable& operator=(const ModelVariable& rhs) = default;
    void markAsFixed();
};

class ModelInfoFromYAMLFile: public ModelInfo {
private:
    std::string model_name;       //
    std::string model_file_path;  //
    std::string citation;         //Citation string
    std::string DOI;              //DOI for the publication (optional)
    std::string data_type_name;   //
    int         num_states;       //number of states
    bool        reversible;       //don't trust this; check against rate matrix
    size_t      rate_matrix_rank; //
    std::vector<StrVector> rate_matrix_expressions; //row major (expression strings)
    std::vector<YAMLFileParameter> parameters;      //parameters
    StateFreqType frequency_type;
    
    std::map<std::string, ModelVariable> variables;
    
    friend class ModelListFromYAMLFile;
    friend class ModelFileLoader;
public:
    ModelInfoFromYAMLFile(); //Only ModelListFromYAMLFile uses it.
    ModelInfoFromYAMLFile(const ModelInfoFromYAMLFile& rhs) = default;
    explicit ModelInfoFromYAMLFile(const std::string& file_path);
    ~ModelInfoFromYAMLFile() = default;
    
    virtual std::string getFreeRateParameters(int& num_rate_cats,
                                              bool &fused_mix_rate) const {
        FUNCTION_NOT_IMPLEMENTED;
        return "";
    }
    
    virtual std::string getFrequencyMixtureParams(std::string& freq_str) const {
        FUNCTION_NOT_IMPLEMENTED;
        return "";
    }
    
    virtual void        getFrequencyOptions(std::string& freq_str,
                                            StateFreqType& freq_type,
                                            std::string& freq_params,
                                            bool& optimize_mixmodel_weight) const {
        FUNCTION_NOT_IMPLEMENTED;
    }
    
    virtual void        getGammaParameters(int& num_rate_cats,
                                           double& gamma_shape) const {
        FUNCTION_NOT_IMPLEMENTED;
    }
    
    virtual std::string getHeterotachyParameters(bool is_mixture_model,
                                                 int& num_rate_cats,
                                                 bool& fused_mix_rate) const {
        FUNCTION_NOT_IMPLEMENTED;
        return "";
    }
    
    virtual double getProportionOfInvariantSites() const { return 0.0; /*stub*/ }

    virtual bool hasAscertainmentBiasCorrection()  const { return false; /*stub*/ }
    virtual bool hasRateHeterotachy()              const { return false; /*stub*/ }
    
    virtual bool isFreeRate()                      const { return false; /*stub*/ }
    virtual bool isFrequencyMixture()              const { return false; /*stub*/ }
    virtual bool isGammaModel()                    const { return false; /*stub*/ }
    virtual bool isInvariantModel()                const { return false; /*stub*/ }
    virtual bool isMixtureModel()                  const { return false; /*stub*/ }
    virtual bool isModelFinder()                   const { return false; /*stub*/ }
    virtual bool isModelFinderOnly()               const { return false; /*stub*/ }
    virtual bool isPolymorphismAware()             const { return false; /*stub*/ }
    virtual bool isReversible()                    const { return reversible; }
    virtual bool isWeissAndVonHaeselerTest()       const { return false; /*stub*/ }

    ASCType extractASCType
        (std::string& leftover_name) const {
        FUNCTION_NOT_IMPLEMENTED;
        return ASC_VARIANT;
    }
    std::string extractMixtureModelList
        (std::string& leftover_name) const {
        FUNCTION_NOT_IMPLEMENTED;
        return "";
    }
    std::string extractPolymorphicHeterozygosity
        (std::string& leftover_name) const {
        FUNCTION_NOT_IMPLEMENTED;
        return "";
    }
    void updateName  (const std::string&       name);
    void addParameter(const YAMLFileParameter& p);
    
public:
    std::string getLongName() const {
        return model_name + " from YAML model file " +
        model_file_path;
    }
    bool hasVariable(const char* name) const {
        return variables.find(name) != variables.end();
    }
    bool hasVariable(const std::string& name) const {
        return variables.find(name) != variables.end();
    }
    double getVariableValue(const std::string& name) const {
        auto found = variables.find(name);
        if (found==variables.end()) {
            return 0.0;
        }
        return found->second.value;
    }
    bool isFrequencyParameter(const std::string& param_name) const;
    void setBounds(int bound_count, double *lower_bound,
                   double *upper_bound, bool *bound_check) const;
    void updateVariables(const double* variables,
                         int param_count);
    ModelVariable& assign(const std::string& var_name, double value);
};

class ModelListFromYAMLFile {
protected:
    std::map<std::string, ModelInfoFromYAMLFile> models_found;

public:
    ModelListFromYAMLFile()  = default;
    ~ModelListFromYAMLFile() = default;

    void loadFromFile          (const char* file_path);
    bool isModelNameRecognized (const char* model_name);
    ModelMarkov* getModelByName(const char* model_name,   PhyloTree *tree,
                                const char* model_params, StateFreqType freq_type,
                                const char* freq_params,  PhyloTree* report_to_tree);
    
    bool hasModel(const std::string& model_name) const;
    const ModelInfoFromYAMLFile& getModel(const std::string& model_name) const;
};

#endif /* modelinfo_hpp */
