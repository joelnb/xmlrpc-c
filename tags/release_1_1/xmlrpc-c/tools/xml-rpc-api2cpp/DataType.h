
class DataType {
    string mTypeName;

    DataType (const DataType&) { XMLRPC_ASSERT(0); }
    DataType& operator= (const DataType&) { XMLRPC_ASSERT(0); return *this; }

public:
    DataType (const string& type_name) : mTypeName(type_name) {}
    virtual ~DataType () {}

    // Return the name for this XML-RPC type.
    virtual string typeName () const { return mTypeName; }

    // Given a parameter position, calculate a unique base name for all
    // parameter-related variables.
    virtual string defaultParameterBaseName (int position) const;

    // Virtual functions for processing parameters.
    virtual string parameterFragment (const string& base_name) const = 0;
    virtual string inputConversionFragment (const string& base_name) const = 0;

    // Virtual functions for processing return values.
    virtual string returnTypeFragment () const = 0;
    virtual string outputConversionFragment (const string& var_name) const = 0;
};

const DataType& findDataType (const string& name);
