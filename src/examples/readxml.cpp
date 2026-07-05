#include <utils.h>
#include <iostream>
#include <map>
#include <string>

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace pt = boost::property_tree;

namespace xmldata
{
    class CampofficialPromoteData
    {
        struct FuncOpen
        {
            uint32_t    open;
            uint32_t    allzone;
            std::string openzones;
            std::string addexpoitendtime;
            uint32_t    seasonawardopen;

            void loadXml(const pt::ptree& root);
            void print();
        };

        FuncOpen funcopen_;

        struct ExploitLimit
        {
            uint32_t daylimit;
            uint32_t weeklimit;
            uint32_t daysublimit;
            uint32_t weeksublimit;

            void loadXml(const pt::ptree& root);
            void print();
        };

        ExploitLimit exploitlimit_;

        struct TopList
        {
            uint32_t    open;
            std::string round;
            uint32_t    maxshow;
            std::string fromtime;
            std::string totime;

            void loadXml(const pt::ptree& root);
            void print();
        };

        TopList tolist_;

        struct AppointOfficial
        {
            bool     open;
            uint32_t minofficialid;
            uint32_t minexploit;
            uint32_t reappointcd;
            uint32_t appointprotecttime;

            struct ExtraOfficial
            {
                int         officialid;
                std::string officialname;
                int         maxnum;

                void loadXml(const pt::ptree& root);
                void print();
            };
            std::map<int, ExtraOfficial> extraicials_map_;

            void loadXml(const pt::ptree& root);
            void print();
        };

        AppointOfficial appointofficial_;

    public:
        void loadXml(const std::string& xml_file_path);
        void print();

    private:
        std::string xml_file_path_;
    };

    void CampofficialPromoteData::loadXml(const std::string& xml_file_path)
    {
        xml_file_path_ = xml_file_path;

        try
        {
            // 解析XML文件
            // 读取 XML 文件
            pt::ptree   tree;
            std::string filename = xml_file_path_;
            pt::read_xml(filename, tree);

            // 获取根节点
            pt::ptree root = tree.get_child("CampOfficialPromote");
            funcopen_.loadXml(root.get_child("funcopen"));
            exploitlimit_.loadXml(root.get_child("exploitlimit"));
            tolist_.loadXml(root.get_child("toplist"));
            appointofficial_.loadXml(root.get_child("appointofficial"));

            print();
        }
        catch (const pt::xml_parser_error& e)
        {
            std::cerr << "XML 解析错误: " << e.what() << std::endl;
        }
        catch (const pt::ptree_bad_path& e)
        {
            std::cerr << "路径查找错误 (可能是标签名拼写错误或属性不存在): " << e.what() << std::endl;
        }
    }

    void CampofficialPromoteData::FuncOpen::loadXml(const pt::ptree& ptree_node)
    {
        open             = cncpp::cast_to<uint32_t>(ptree_node.get<std::string>("<xmlattr>.open"));
        allzone          = cncpp::cast_to<uint32_t>(ptree_node.get<std::string>("<xmlattr>.allzone"));
        openzones        = ptree_node.get<std::string>("<xmlattr>.openzones");
        addexpoitendtime = ptree_node.get<std::string>("<xmlattr>.addexpoitendtime");
        seasonawardopen  = cncpp::cast_to<uint32_t>(ptree_node.get<std::string>("<xmlattr>.seasonawardopen"));
    }

    void CampofficialPromoteData::FuncOpen::print()
    {
        std::cout << "FuncOpen:" << std::endl;
        std::cout << "open: " << std::boolalpha << open << std::endl;
        std::cout << "allzone: " << std::boolalpha << allzone << std::endl;
        std::cout << "openzones: " << openzones << std::endl;
        std::cout << "addexpoitendtime: " << addexpoitendtime << std::endl;
        std::cout << "seasonawardopen: " << std::boolalpha << seasonawardopen << std::endl;
        std::cout << std::endl;
    }

    void CampofficialPromoteData::TopList::loadXml(const pt::ptree& ptree_node)
    {
        open     = ptree_node.get<bool>("<xmlattr>.open");
        round    = ptree_node.get<std::string>("<xmlattr>.round");
        maxshow  = ptree_node.get<int>("<xmlattr>.maxshow");
        fromtime = ptree_node.get<std::string>("<xmlattr>.fromtime");
        totime   = ptree_node.get<std::string>("<xmlattr>.totime");
    }

    void CampofficialPromoteData::TopList::print()
    {
        std::cout << "TopList:" << std::endl;
        std::cout << "open: " << std::boolalpha << open << std::endl;
        std::cout << "round: " << round << std::endl;
        std::cout << "maxshow: " << maxshow << std::endl;
        std::cout << "fromtime: " << fromtime << std::endl;
        std::cout << "totime: " << totime << std::endl;
        std::cout << "-----------------" << std::endl;
    }

    void CampofficialPromoteData::ExploitLimit::loadXml(const pt::ptree& ptree_node)
    {
        daylimit     = ptree_node.get<int>("<xmlattr>.daylimit");
        weeklimit    = ptree_node.get<int>("<xmlattr>.weeklimit");
        daysublimit  = ptree_node.get<int>("<xmlattr>.daysublimit");
        weeksublimit = ptree_node.get<int>("<xmlattr>.weeksublimit");
    }

    void CampofficialPromoteData::ExploitLimit::print()
    {
        std::cout << "ExploitLimit:" << std::endl;
        std::cout << "daylimit: " << daylimit << std::endl;
        std::cout << "weeklimit: " << weeklimit << std::endl;
        std::cout << "daysublimit: " << daysublimit << std::endl;
        std::cout << "weeksublimit: " << weeksublimit << std::endl;
        std::cout << "-----------------" << std::endl;
    }

    void CampofficialPromoteData::print()
    {
        funcopen_.print();
        exploitlimit_.print();
        tolist_.print();
        appointofficial_.print();
    }

    void CampofficialPromoteData::AppointOfficial::loadXml(const pt::ptree& ptree_node)
    {
        open               = ptree_node.get<bool>("<xmlattr>.open");
        minofficialid      = ptree_node.get<int>("<xmlattr>.minofficialid");
        minexploit         = ptree_node.get<int>("<xmlattr>.minexploit");
        reappointcd        = ptree_node.get<int>("<xmlattr>.reappointcd");
        appointprotecttime = ptree_node.get<int>("<xmlattr>.appointprotecttime");

        // 解析 ExtraOfficial 节点
        extraicials_map_.clear();
        auto range = ptree_node.equal_range("extraofficial");
        for (auto it = range.first; it != range.second; ++it)
        {
            ExtraOfficial extraicial = {};
            extraicial.loadXml(it->second);
            extraicials_map_[extraicial.officialid] = extraicial;
        }
    }

    void CampofficialPromoteData::AppointOfficial::print()
    {
        std::cout << "open: " << std::boolalpha << open << std::endl;
        std::cout << "minofficialid: " << minofficialid << std::endl;
        std::cout << "minexploit: " << minexploit << std::endl;
        std::cout << "reappointcd: " << reappointcd << std::endl;
        std::cout << "appointprotecttime: " << appointprotecttime << std::endl;
        std::cout << "-----------------" << std::endl;

        for (auto& item : extraicials_map_)
        {
            item.second.print();
            std::cout << "-----------------" << std::endl;
        }
    }

    void CampofficialPromoteData::AppointOfficial::ExtraOfficial::loadXml(const pt::ptree& ptree_node)
    {
        officialid   = ptree_node.get<int>("<xmlattr>.officialid");
        officialname = ptree_node.get<std::string>("<xmlattr>.officialname");
        maxnum       = ptree_node.get<int>("<xmlattr>.maxnum");
    }

    void CampofficialPromoteData::AppointOfficial::ExtraOfficial::print()
    {
        std::cout << "ExtraOfficial:" << std::endl;
        std::cout << "officialid: " << officialid << std::endl;
        std::cout << "officialname: " << officialname << std::endl;
        std::cout << "maxnum: " << maxnum << std::endl;
        std::cout << "-----------------" << std::endl;
    }
}  // namespace xmldata

int main()
{
    xmldata::CampofficialPromoteData campofficial_promote_data;
    campofficial_promote_data.loadXml("../src/config/test_official.xml");
    return 0;
}