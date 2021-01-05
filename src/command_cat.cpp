/*

Osmium -- OpenStreetMap data manipulation command line tool
https://osmcode.org/osmium-tool/

Copyright (C) 2013-2021  Jochen Topf <jochen@topf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#include "command_cat.hpp"
#include "exception.hpp"
#include "util.hpp"

#include <osmium/io/file.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/object.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/util/verbose_output.hpp>

#include <boost/program_options.hpp>

#include <string>
#include <utility>
#include <vector>

bool CommandCat::setup(const std::vector<std::string>& arguments) {
    po::options_description opts_cmd{"COMMAND OPTIONS"};
    opts_cmd.add_options()
    ("object-type,t", po::value<std::vector<std::string>>(), "Read only objects of given type (node, way, relation, changeset)")
    ("clean,c", po::value<std::vector<std::string>>(), "Clean attribute (version, changeset, timestamp, uid, user)")
    ;

    po::options_description opts_common{add_common_options()};
    po::options_description opts_input{add_multiple_inputs_options()};
    po::options_description opts_output{add_output_options()};

    po::options_description hidden;
    hidden.add_options()
    ("input-filenames", po::value<std::vector<std::string>>(), "Input files")
    ;

    po::options_description desc;
    desc.add(opts_cmd).add(opts_common).add(opts_input).add(opts_output);

    po::options_description parsed_options;
    parsed_options.add(desc).add(hidden);

    po::positional_options_description positional;
    positional.add("input-filenames", -1);

    po::variables_map vm;
    po::store(po::command_line_parser(arguments).options(parsed_options).positional(positional).run(), vm);
    po::notify(vm);

    setup_common(vm, desc);
    setup_progress(vm);
    setup_object_type_nwrc(vm);
    setup_input_files(vm);
    setup_output_file(vm);

    if (vm.count("clean")) {
        for (const auto& c : vm["clean"].as<std::vector<std::string>>()) {
            if (c == "version") {
                m_clean_attrs |= clean_options::clean_version;
            } else if (c == "changeset") {
                m_clean_attrs |= clean_options::clean_changeset;
            } else if (c == "timestamp") {
                m_clean_attrs |= clean_options::clean_timestamp;
            } else if (c == "uid") {
                m_clean_attrs |= clean_options::clean_uid;
            } else if (c == "user") {
                m_clean_attrs |= clean_options::clean_user;
            } else {
                throw argument_error{"Unknown attribute on -c/--clean option: '" + c +"'"};
            }
        }
    }

    return true;
}

void CommandCat::show_arguments() {
    show_multiple_inputs_arguments(m_vout);
    show_output_arguments(m_vout);

    m_vout << "  other options:\n";
    show_object_types(m_vout);

    std::string clean_names;
    if (m_clean_attrs & clean_options::clean_version) {
        clean_names += "version,";
    }
    if (m_clean_attrs & clean_options::clean_changeset) {
        clean_names += "changeset,";
    }
    if (m_clean_attrs & clean_options::clean_timestamp) {
        clean_names += "timestamp,";
    }
    if (m_clean_attrs & clean_options::clean_uid) {
        clean_names += "uid,";
    }
    if (m_clean_attrs & clean_options::clean_user) {
        clean_names += "user,";
    }

    if (clean_names.empty()) {
        clean_names = "(none)";
    } else {
        clean_names.resize(clean_names.size() - 1);
    }

    m_vout << "    attributes to clean: " << clean_names << '\n';
}

void CommandCat::copy(osmium::ProgressBar& progress_bar, osmium::io::Reader& reader, osmium::io::Writer &writer) const {
    while (osmium::memory::Buffer buffer = reader.read()) {
        progress_bar.update(reader.offset());

        if (m_clean_attrs) {
            for (auto& object : buffer.select<osmium::OSMObject>()) {
                if (m_clean_attrs & clean_options::clean_version) {
                    object.set_version(static_cast<osmium::object_version_type>(0));
                }
                if (m_clean_attrs & clean_options::clean_changeset) {
                    object.set_changeset(static_cast<osmium::changeset_id_type>(0));
                }
                if (m_clean_attrs & clean_options::clean_timestamp) {
                    object.set_timestamp(osmium::Timestamp{});
                }
                if (m_clean_attrs & clean_options::clean_uid) {
                    object.set_uid(static_cast<osmium::user_id_type>(0));
                }
                if (m_clean_attrs & clean_options::clean_user) {
                    object.clear_user();
                }
            }
        }

        writer(std::move(buffer));
    }
}

bool CommandCat::run() {
    std::size_t file_size = 0;

    if (m_input_files.size() == 1) { // single input file
        osmium::io::Reader reader{m_input_files[0], osm_entity_bits()};
        osmium::io::Header header{reader.header()};
        m_vout << "Copying input file '" << m_input_files[0].filename()
               << "' (" << reader.file_size() << " bytes)\n";

        setup_header(header);
        osmium::io::Writer writer(m_output_file, header, m_output_overwrite, m_fsync);

        osmium::ProgressBar progress_bar{reader.file_size(), display_progress()};
        copy(progress_bar, reader, writer);
        progress_bar.done();
        file_size = writer.close();
        reader.close();
    } else { // multiple input files
        osmium::io::Header header;
        setup_header(header);
        osmium::io::Writer writer{m_output_file, header, m_output_overwrite, m_fsync};

        osmium::ProgressBar progress_bar{file_size_sum(m_input_files), display_progress()};
        for (const auto& input_file : m_input_files) {
            progress_bar.remove();
            osmium::io::Reader reader{input_file, osm_entity_bits()};
            m_vout << "Copying input file '" << input_file.filename()
                   << "' (" << reader.file_size() << " bytes)\n";
            copy(progress_bar, reader, writer);
            progress_bar.file_done(reader.file_size());
            reader.close();
        }
        file_size = writer.close();
        progress_bar.done();
    }

    if (file_size > 0) {
        m_vout << "Wrote " << file_size << " bytes.\n";
    }

    show_memory_used();
    m_vout << "Done.\n";

    return true;
}

