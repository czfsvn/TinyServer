#include "CampofficialPromote.h"
#include <boost/property_tree/xml_parser.hpp>

namespace xml
{

// ==================== CampofficialPromote ====================

bool xml::CampofficialPromote::loadXml(const boost::property_tree::ptree& root)
{
    try
    {
        for (const auto& child : root)
        {
            if (child.first == "<xmlattr>")
                continue;

            if (child.first == "funcopen")
            {
                if (!funcopen.loadXml(child.second))
                    return false;
            }
            else if (child.first == "exploitlimit")
            {
                if (!exploitlimit.loadXml(child.second))
                    return false;
            }
            else if (child.first == "toplist")
            {
                if (!toplist.loadXml(child.second))
                    return false;
            }
            else if (child.first == "appointofficial")
            {
                if (!appointofficial.loadXml(child.second))
                    return false;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[CampofficialPromote] loadXml failed: " << e.what() << std::endl;
        return false;
    }
    return true;
}

bool xml::CampofficialPromote::loadXml(const std::string& xml_file_path)
{
    xml_file_path_ = xml_file_path;
    try
    {
        boost::property_tree::ptree tree;
        boost::property_tree::read_xml(xml_file_path_, tree);
        return loadXml(tree.get_child("CampofficialPromote"));
    }
    catch (const std::exception& e)
    {
        std::cerr << "[CampofficialPromote] failed to read file \"" << xml_file_path_ << "\": " << e.what() << std::endl;
        return false;
    }
}

void xml::CampofficialPromote::dumpAll() const
{
    std::cout << "[funcopen]" << std::endl;
    funcopen.dumpAll();
    std::cout << "[exploitlimit]" << std::endl;
    exploitlimit.dumpAll();
    std::cout << "[toplist]" << std::endl;
    toplist.dumpAll();
    std::cout << "[appointofficial]" << std::endl;
    appointofficial.dumpAll();
}

void xml::CampofficialPromote::clear()
{
    funcopen.clear();
    exploitlimit.clear();
    toplist.clear();
    appointofficial.clear();
}

// ==================== CampofficialPromote::FuncOpen ====================

bool xml::CampofficialPromote::FuncOpen::loadXml(const boost::property_tree::ptree& root)
{
    try
    {
        open_ = root.get<uint32_t>("<xmlattr>.open", 0);
        allzone_ = root.get<uint32_t>("<xmlattr>.allzone", 0);
        openzones_ = root.get<std::string>("<xmlattr>.openzones", "");
        addexpoitendtime_ = root.get<std::string>("<xmlattr>.addexpoitendtime", "");
        seasonawardopen_ = root.get<uint32_t>("<xmlattr>.seasonawardopen", 0);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FuncOpen] loadXml failed: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void xml::CampofficialPromote::FuncOpen::dumpAll() const
{
    std::cout << "  open: " << open_ << std::endl;
    std::cout << "  allzone: " << allzone_ << std::endl;
    std::cout << "  openzones: " << openzones_ << std::endl;
    std::cout << "  addexpoitendtime: " << addexpoitendtime_ << std::endl;
    std::cout << "  seasonawardopen: " << seasonawardopen_ << std::endl;
}

void xml::CampofficialPromote::FuncOpen::clear()
{
    open_ = 0;
    allzone_ = 0;
    openzones_ = "";
    addexpoitendtime_ = "";
    seasonawardopen_ = 0;
}

// ==================== CampofficialPromote::ExpLoitlimit ====================

bool xml::CampofficialPromote::ExpLoitlimit::loadXml(const boost::property_tree::ptree& root)
{
    try
    {
        daylimit_ = root.get<uint32_t>("<xmlattr>.daylimit", 0);
        weeklimit_ = root.get<uint32_t>("<xmlattr>.weeklimit", 0);
        daysublimit_ = root.get<uint32_t>("<xmlattr>.daysublimit", 0);
        weeksublimit_ = root.get<uint32_t>("<xmlattr>.weeksublimit", 0);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ExpLoitlimit] loadXml failed: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void xml::CampofficialPromote::ExpLoitlimit::dumpAll() const
{
    std::cout << "  daylimit: " << daylimit_ << std::endl;
    std::cout << "  weeklimit: " << weeklimit_ << std::endl;
    std::cout << "  daysublimit: " << daysublimit_ << std::endl;
    std::cout << "  weeksublimit: " << weeksublimit_ << std::endl;
}

void xml::CampofficialPromote::ExpLoitlimit::clear()
{
    daylimit_ = 0;
    weeklimit_ = 0;
    daysublimit_ = 0;
    weeksublimit_ = 0;
}

// ==================== CampofficialPromote::TopList ====================

bool xml::CampofficialPromote::TopList::loadXml(const boost::property_tree::ptree& root)
{
    try
    {
        open_ = root.get<uint32_t>("<xmlattr>.open", 0);
        round_ = root.get<uint32_t>("<xmlattr>.round", 0);
        maxshow_ = root.get<uint32_t>("<xmlattr>.maxshow", 0);
        fromtime_ = root.get<std::string>("<xmlattr>.fromtime", "");
        totime_ = root.get<std::string>("<xmlattr>.totime", "");
    }
    catch (const std::exception& e)
    {
        std::cerr << "[TopList] loadXml failed: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void xml::CampofficialPromote::TopList::dumpAll() const
{
    std::cout << "  open: " << open_ << std::endl;
    std::cout << "  round: " << round_ << std::endl;
    std::cout << "  maxshow: " << maxshow_ << std::endl;
    std::cout << "  fromtime: " << fromtime_ << std::endl;
    std::cout << "  totime: " << totime_ << std::endl;
}

void xml::CampofficialPromote::TopList::clear()
{
    open_ = 0;
    round_ = 0;
    maxshow_ = 0;
    fromtime_ = "";
    totime_ = "";
}

// ==================== CampofficialPromote::AppointOfficial ====================

bool xml::CampofficialPromote::AppointOfficial::loadXml(const boost::property_tree::ptree& root)
{
    try
    {
        open_ = root.get<uint32_t>("<xmlattr>.open", 0);
        minofficialid_ = root.get<uint32_t>("<xmlattr>.minofficialid", 0);
        minexploit_ = root.get<uint32_t>("<xmlattr>.minexploit", 0);
        reappointcd_ = root.get<uint32_t>("<xmlattr>.reappointcd", 0);
        appointprotecttime_ = root.get<uint32_t>("<xmlattr>.appointprotecttime", 0);

        for (const auto& child : root)
        {
            if (child.first == "<xmlattr>")
                continue;

            if (child.first == "extraofficial")
            {
                ExtraOfficial item;
                if (!item.loadXml(child.second))
                    return false;
                extraofficial[item.getOfficialId()] = std::move(item);
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[AppointOfficial] loadXml failed: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void xml::CampofficialPromote::AppointOfficial::dumpAll() const
{
    std::cout << "  open: " << open_ << std::endl;
    std::cout << "  minofficialid: " << minofficialid_ << std::endl;
    std::cout << "  minexploit: " << minexploit_ << std::endl;
    std::cout << "  reappointcd: " << reappointcd_ << std::endl;
    std::cout << "  appointprotecttime: " << appointprotecttime_ << std::endl;

    std::cout << "[extraofficial] count=" << extraofficial.size() << std::endl;
    for (const auto& pair : extraofficial)
    {
        pair.second.dumpAll();
    }
}

void xml::CampofficialPromote::AppointOfficial::clear()
{
    open_ = 0;
    minofficialid_ = 0;
    minexploit_ = 0;
    reappointcd_ = 0;
    appointprotecttime_ = 0;
    extraofficial.clear();
}

// ==================== CampofficialPromote::AppointOfficial::ExtraOfficial ====================

bool xml::CampofficialPromote::AppointOfficial::ExtraOfficial::loadXml(const boost::property_tree::ptree& root)
{
    try
    {
        officialid_ = root.get<uint32_t>("<xmlattr>.officialid", 0);
        officialname_ = root.get<std::string>("<xmlattr>.officialname", "");
        maxnum_ = root.get<uint32_t>("<xmlattr>.maxnum", 0);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ExtraOfficial] loadXml failed: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void xml::CampofficialPromote::AppointOfficial::ExtraOfficial::dumpAll() const
{
    std::cout << "  officialid: " << officialid_ << std::endl;
    std::cout << "  officialname: " << officialname_ << std::endl;
    std::cout << "  maxnum: " << maxnum_ << std::endl;
}

void xml::CampofficialPromote::AppointOfficial::ExtraOfficial::clear()
{
    officialid_ = 0;
    officialname_ = "";
    maxnum_ = 0;
}

} // namespace xml
