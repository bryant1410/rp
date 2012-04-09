#include "program.hpp"

#include <iostream>
#include <map>
#include <sstream>

#include "executable_format.hpp"
#include "raw.hpp"
#include "ia32.hpp"
#include "ia64.hpp"
#include "section.hpp"
#include "coloshell.hpp"
#include "rpexception.hpp"
#include "toolbox.hpp"

Program::Program(const std::string & program_path, CPU::E_CPU arch)
: m_cpu(NULL), m_exformat(NULL)
{
    unsigned int magic_dword = 0;

    std::cout << "Trying to open '" << program_path << "'.." << std::endl;
    m_file.open(program_path.c_str(), std::ios::binary);
    if(m_file.is_open() == false)
        RAISE_EXCEPTION("Cannot open the file");

    /* If we know the CPU in the constructor, it is a raw file */
    if(arch != CPU::CPU_UNKNOWN)
    {
        m_exformat = new (std::nothrow) Raw();
        if(m_exformat == NULL)
            RAISE_EXCEPTION("Cannot allocate raw");
        
        switch(arch)
        {
            case CPU::CPU_IA32:
                m_cpu = new (std::nothrow) Ia32();
                break;

            case CPU::CPU_IA64:
                m_cpu = new (std::nothrow) Ia64();
                break;

            default:
                RAISE_EXCEPTION("Don't know your architecture");
        }

        if(m_cpu == NULL)
            RAISE_EXCEPTION("Cannot allocate m_cpu");
 
    }
    /* This isn't a raw file, we have to determine the executable format and the cpu */
    else
    {
        m_file.read((char*)&magic_dword, sizeof(magic_dword));

        m_exformat = ExecutableFormat::GetExecutableFormat(magic_dword);
        if(m_exformat == NULL)
            RAISE_EXCEPTION("GetExecutableFormat fails");

        m_cpu = m_exformat->get_cpu(m_file);
        if(m_cpu == NULL)
            RAISE_EXCEPTION("get_cpu fails");
    }


    std::cout << "FileFormat: " << m_exformat->get_class_name() << ", Arch: " << m_cpu->get_class_name() << std::endl;
}

Program::~Program(void)
{
    if(m_file.is_open())
        m_file.close();
    
    if(m_exformat != NULL)
        delete m_exformat;

    if(m_cpu != NULL)
        delete m_cpu;
}

void Program::display_information(VerbosityLevel lvl)
{
    m_exformat->display_information(lvl);
}

std::map<std::string, Gadget*> Program::find_gadgets(unsigned int depth)
{
    std::map<std::string, Gadget*> gadgets_found;

    /* To do a ROP gadget research, we need to know the executable section */
    std::vector<Section*> executable_sections = m_exformat->get_executables_section(m_file);
    if(executable_sections.size() == 0)
        std::cout << "It seems your binary haven't executable sections." << std::endl;

    /* Walk the executable sections */
    for(std::vector<Section*>::iterator it_sec = executable_sections.begin(); it_sec != executable_sections.end(); ++it_sec)
    {
        std::cout << "in " << (*it_sec)->get_name() << ".. ";
        unsigned long long va_section = m_exformat->raw_offset_to_va((*it_sec)->get_offset(), (*it_sec)->get_offset());

        /* Let the cpu do the research (BTW we use a std::map in order to keep only unique gadget) */
        std::list<Gadget*> gadgets = m_cpu->find_gadget_in_memory(
            (*it_sec)->get_section_buffer(),
            (*it_sec)->get_size(),
            va_section,
            depth
        );

        /* Now we have a list of gadget cool, but we want to keep only the unique! */
        for(std::list<Gadget*>::const_iterator it_g = gadgets.begin(); it_g != gadgets.end(); ++it_g)
        {
            /* If a gadget, with the same disassembly, has already been found ; just add its offset in the existing one */
            if(gadgets_found.count((*it_g)->get_disassembly()) > 0)
            {
                std::map<std::string, Gadget*>::iterator g = gadgets_found.find((*it_g)->get_disassembly());
                
                /*
                    we have found the same gadget in memory, so we just store its offset & its va section 
                    Why we store its va section ? Because you can find the same gadget in another executable sections!
                */
                g->second->add_new_one((*it_g)->get_first_offset(),
                    va_section
                );
            }
            else
            {
                gadgets_found.insert(std::make_pair(
                    (*it_g)->get_disassembly(),
                    (*it_g)
                ));
            }
        }
    }

    return gadgets_found;
}

void Program::search_and_display(const unsigned char* hex_values, unsigned int size)
{
    std::vector<Section*> executable_sections = m_exformat->get_executables_section(m_file);
    if(executable_sections.size() == 0)
        std::cout << "It seems your binary haven't executable sections." << std::endl;

    for(std::vector<Section*>::iterator it = executable_sections.begin(); it != executable_sections.end(); ++it)
    {
        std::list<unsigned long long> ret = (*it)->search_in_memory(hex_values, size);
        for(std::list<unsigned long long>::iterator it2 = ret.begin(); it2 != ret.end(); ++it2)
        {
            unsigned long long va_section = m_exformat->raw_offset_to_va((*it)->get_offset(), (*it)->get_offset());
            unsigned long long va = va_section + *it2;
            
            display_offset_lf(va, hex_values, size);
        }
    }
}
