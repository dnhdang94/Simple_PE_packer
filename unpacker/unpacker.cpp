//Include structures file from packer project
#include "../simple_pe_packer/structs.h"

//Unpacking algorithm
#include "lzo_conf.h"
/* decompression */
LZO_EXTERN(int)
lzo1z_decompress        ( const lzo_bytep src, lzo_uint  src_len,
                                lzo_bytep dst, lzo_uintp dst_len,
                                lzo_voidp wrkmem /* NOT USED */ );

//Create function without prologue and epilogue
extern "C" void __declspec(naked) unpacker_main()
{
	//Create prologue manually
	__asm
	{
		jmp next;
		ret 0xC;
next:

		push ebp;
		mov ebp, esp;
		sub esp, 4096;
		
		mov eax, 0x11111111;
		mov ecx, 0x22222222;
		mov edx, 0x33333333;
	}
	
	//Image loading address
	unsigned int original_image_base;
	//First section relative address,
	//in which the packer stores its information
	//and packed data themselves
	unsigned int rva_of_first_section;
	//Image loading address (Original one, relocations are not applied to it)
	unsigned int original_image_base_no_fixup;
	
	//These instructions are required only to
	//replace the addresses in unpacker builder with real ones
	__asm
	{
		mov original_image_base, eax;
		mov rva_of_first_section, ecx;
		mov original_image_base_no_fixup, edx;
	}
	
	//Address of the variable,
	//which indicates if code was unpacked already
	DWORD* was_unpacked;

	__asm
	{
		//Trick to get address
		//of instruction following "call"
		call next2;
		add byte ptr [eax], al;
		add byte ptr [eax], al;
next2:
		//There is an address of first instruction
		//add byte ptr [eax], al
		//in eax
		pop eax;

		//Store this address
		mov was_unpacked, eax;

		//Check what is stored there
		mov eax, [eax];

		//If there is zero, then move to
		//the unpacker
		test eax, eax;
		jz next3;

		//If not, then finish the unpacker
		//and go to original entry point
		leave;
		jmp eax;

next3:
	}
	
	//Get pointer to structure with information
	//carefully prepared by packer
	const packed_file_info* info;
	//It is stored in the beginning
	//of packed file first section
	info = reinterpret_cast<const packed_file_info*>(original_image_base + rva_of_first_section);

	//Get original entry point address
	DWORD original_ep;
	original_ep = info->original_entry_point + original_image_base;

	__asm
	{
		//Write it to address stored in
		//was_unpacked variable
		mov edx, was_unpacked;
		mov eax, original_ep;
		mov [edx], eax;
	}
	
	//Two LoadLibraryA and GetProcAddress function prototypes typedefs 
	typedef HMODULE (__stdcall* load_library_a_func)(const char* library_name);
	typedef INT_PTR (__stdcall* get_proc_address_func)(HMODULE dll, const char* func_name);

	//Read their addresses from packed_file_info structure
	//Loader puts them there for us
	load_library_a_func load_library_a;
	get_proc_address_func get_proc_address;
	load_library_a = reinterpret_cast<load_library_a_func>(info->load_library_a);
	get_proc_address = reinterpret_cast<get_proc_address_func>(info->get_proc_address);
	
	
	//Create buffer on stack
	char buf[32];
	//kernel32.dll
	*reinterpret_cast<DWORD*>(&buf[0]) = 'nrek';
	*reinterpret_cast<DWORD*>(&buf[4]) = '23le';
	*reinterpret_cast<DWORD*>(&buf[8]) = 'lld.';
	*reinterpret_cast<DWORD*>(&buf[12]) = 0;

	//Load kernel32.dll library
	HMODULE kernel32_dll;
	kernel32_dll = load_library_a(buf);

	//VirtualAlloc function prototype typedef
	typedef LPVOID (__stdcall* virtual_alloc_func)(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
	//VirtualProtect function prototype typedef
	typedef LPVOID (__stdcall* virtual_protect_func)(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect);
	//VirtualFree function prototype typedef
	typedef LPVOID (__stdcall* virtual_free_func)(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);

	//VirtualAlloc
	*reinterpret_cast<DWORD*>(&buf[0]) = 'triV';
	*reinterpret_cast<DWORD*>(&buf[4]) = 'Alau';
	*reinterpret_cast<DWORD*>(&buf[8]) = 'coll';
	*reinterpret_cast<DWORD*>(&buf[12]) = 0;

	//Get VirtualAlloc function address
	virtual_alloc_func virtual_alloc;
	virtual_alloc = reinterpret_cast<virtual_alloc_func>(get_proc_address(kernel32_dll, buf));

	//VirtualProtect
	*reinterpret_cast<DWORD*>(&buf[0]) = 'triV';
	*reinterpret_cast<DWORD*>(&buf[4]) = 'Plau';
	*reinterpret_cast<DWORD*>(&buf[8]) = 'etor';
	*reinterpret_cast<DWORD*>(&buf[12]) = 'tc';

	//Get VirtualProtect function address
	virtual_protect_func virtual_protect;
	virtual_protect = reinterpret_cast<virtual_protect_func>(get_proc_address(kernel32_dll, buf));

	//VirtualFree
	*reinterpret_cast<DWORD*>(&buf[0]) = 'triV';
	*reinterpret_cast<DWORD*>(&buf[4]) = 'Flau';
	*reinterpret_cast<DWORD*>(&buf[8]) = 'eer';

	//Get VirtualFree function address
	virtual_free_func virtual_free;
	virtual_free = reinterpret_cast<virtual_free_func>(get_proc_address(kernel32_dll, buf));
	
	//Copy all packed_file_info structure fields, because
	//we will need them further, but we will overwrite the structure at "info" pointer soon
	packed_file_info info_copy;
	memcpy(&info_copy, info, sizeof(info_copy));
	
	//Pointer to the memory 
	//to store unpacked data
	LPVOID unpacked_mem;
	//Allocate the memory
	unpacked_mem = virtual_alloc(
		0,
		info->size_of_unpacked_data,
		MEM_COMMIT,
		PAGE_READWRITE);

	//Unpacked data size
	//(in fact, this variable is unnecessary)
	lzo_uint out_len;
	out_len = 0;

	//Unpack with LZO algorithm
	if (LZO_E_OK !=
		lzo1z_decompress(
			reinterpret_cast<const unsigned char*>(reinterpret_cast<DWORD>(info) + sizeof(packed_file_info)),
			info->size_of_packed_data,
			reinterpret_cast<unsigned char*>(unpacked_mem),
			&out_len,
			0)
		)
	{
		//If something goes wrong, but
		// naked function can not return;
	}
	
	
	//Pointer to DOS file header
	const IMAGE_DOS_HEADER* dos_header_org;
	//Pointer to file header
	IMAGE_FILE_HEADER* file_header_org;
	//Pointer to NT header
	PIMAGE_NT_HEADERS nt_headers_org;
	//Virtual address of sections header beginning
	DWORD offset_to_section_headers_org;
	//Calculate this address
	dos_header_org = reinterpret_cast<const IMAGE_DOS_HEADER*>(unpacked_mem);
	nt_headers_org = reinterpret_cast<const PIMAGE_NT_HEADERS>(reinterpret_cast<char *>(unpacked_mem) + dos_header_org->e_lfanew);
	file_header_org = &(nt_headers_org->FileHeader);
	//with this formula
	offset_to_section_headers_org = reinterpret_cast<DWORD>(unpacked_mem) + dos_header_org->e_lfanew + file_header_org->SizeOfOptionalHeader
		+ sizeof(IMAGE_FILE_HEADER) + sizeof(DWORD) /* Signature */;
	
	//Pointer to DOS file header
	const IMAGE_DOS_HEADER* dos_header;
	//Pointer to file header
	IMAGE_FILE_HEADER* file_header;
	//Pointer to NT header
	PIMAGE_NT_HEADERS nt_headers;
	//Virtual address of sections header beginning
	DWORD offset_to_section_headers;
	//Calculate this address
	dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(original_image_base);
	nt_headers = reinterpret_cast<const PIMAGE_NT_HEADERS>(original_image_base + dos_header->e_lfanew);
	file_header = &(nt_headers->FileHeader);
	//with this formula
	offset_to_section_headers = original_image_base + dos_header->e_lfanew + file_header->SizeOfOptionalHeader
		+ sizeof(IMAGE_FILE_HEADER) + sizeof(DWORD) /* Signature */;

	//Null first section memory
	//This region matches the memory region,
	//which is occupied by all sections in original file
	//Only can use info_copy from now, because info will be 0
	memset(
		reinterpret_cast<void*>(original_image_base + rva_of_first_section),
		0,
		info_copy.total_virtual_size_of_sections - rva_of_first_section);

	//Let's change memory block attributes, in which
	//PE file and section headers are placed
	//We need write access
	DWORD old_protect;
	virtual_protect(reinterpret_cast<LPVOID>(original_image_base),
		rva_of_first_section,
		PAGE_READWRITE, &old_protect);


	//Now we change section number
	//in PE file header to original
	file_header->NumberOfSections = info_copy.number_of_sections;
	//Copy filled header
	//to memory, where section headers are stored
	memcpy(reinterpret_cast<void*>(offset_to_section_headers), reinterpret_cast<void*>(offset_to_section_headers_org), sizeof(IMAGE_SECTION_HEADER) * (file_header->NumberOfSections));
	
	PIMAGE_SECTION_HEADER section_header;
	section_header = IMAGE_FIRST_SECTION(nt_headers);
	//List all the sections again
	for(int i = 0; i < info_copy.number_of_sections; ++i, ++section_header)
	{
		//Copying sections data to the place in memory,
		//where they have to be placed
		memcpy(reinterpret_cast<void*>(original_image_base + section_header->VirtualAddress),
			reinterpret_cast<char*>(unpacked_mem) + section_header->PointerToRawData,
			section_header->SizeOfRawData);
	}
	//Size of directory table
	DWORD size_of_directories;
	size_of_directories = sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
	//Calculate relative virtual address
	//of directory table beginning
	DWORD offset_to_directories_org;
	offset_to_directories_org = offset_to_section_headers_org - size_of_directories;
	//Calculate relative virtual address
	//of directory table beginning
	DWORD offset_to_directories;
	offset_to_directories = offset_to_section_headers - size_of_directories;
	//FIXME: 
	memcpy(reinterpret_cast<void*>(offset_to_directories), reinterpret_cast<void*>(offset_to_directories_org), size_of_directories);

	//Release memory with unpacked data,
	//we don't need it anymore
	virtual_free(unpacked_mem, 0, MEM_RELEASE);
	
	//Pointer to import directory
	IMAGE_DATA_DIRECTORY* import_dir;
	import_dir = reinterpret_cast<IMAGE_DATA_DIRECTORY*>(offset_to_directories + sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_DIRECTORY_ENTRY_IMPORT);

	//If the file has imports
	if(import_dir->VirtualAddress)
	{
		//First descriptor virtual address
		IMAGE_IMPORT_DESCRIPTOR* descr;
		descr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(import_dir->VirtualAddress + original_image_base);

		//List all descriptors
		//Last one is nulled
		while(descr->Name)
		{
			//Load the required DLL
			HMODULE dll;
			dll = load_library_a(reinterpret_cast<char*>(descr->Name + original_image_base));
			//Pointers to address table and lookup table
			DWORD* lookup, *address;
			//Take into account that lookup table may be absent,
			//as I mentioned at previous step
			lookup = reinterpret_cast<DWORD*>(original_image_base + (descr->OriginalFirstThunk ? descr->OriginalFirstThunk : descr->FirstThunk));
			address = reinterpret_cast<DWORD*>(descr->FirstThunk + original_image_base);

			//List all descriptor imports
			while(true)
			{
				//Till the first null element in lookup table
				DWORD lookup_value = *lookup;
				if(!lookup_value)
					break;

				//Check if the function is imported by ordinal
				if(IMAGE_SNAP_BY_ORDINAL32(lookup_value))
					*address = static_cast<DWORD>(get_proc_address(dll, reinterpret_cast<const char*>(lookup_value & ~IMAGE_ORDINAL_FLAG32)));
				else
					*address = static_cast<DWORD>(get_proc_address(dll, reinterpret_cast<const char*>(lookup_value + original_image_base + sizeof(WORD))));

				//Move to next element
				++lookup;
				++address;
			}

			//Move to next descriptor
			++descr;
		}
	}
	/*
	// adjust base address of imported data
	DWORD locationDelta;
	locationDelta = (nt_headers->OptionalHeader.ImageBase - nt_headers_org->OptionalHeader.ImageBase);

	// Need relocation
	if (locationDelta)
	{
		//Pointer to relocation directory
		IMAGE_DATA_DIRECTORY* relocation_dir;
		relocation_dir = reinterpret_cast<IMAGE_DATA_DIRECTORY*>(offset_to_directories + sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_DIRECTORY_ENTRY_BASERELOC);
		//If a file had relocations and it
		//was moved by the loader
		if (relocation_dir->VirtualAddress)
		{
			//Pointer to a first IMAGE_BASE_RELOCATION structure
			const IMAGE_BASE_RELOCATION* reloc = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(relocation_dir->VirtualAddress + original_image_base);

			//List relocation tables
			while (reloc->VirtualAddress > 0)
			{
				//List all elements in a table
				for (unsigned long i = sizeof(IMAGE_BASE_RELOCATION); i < reloc->SizeOfBlock; i += sizeof(WORD))
				{
					//Relocation value
					WORD elem = *reinterpret_cast<const WORD*>(reinterpret_cast<const char*>(reloc) + i);
					//If this is IMAGE_REL_BASED_HIGHLOW relocation (there are no other in PE x86)
					if ((elem >> 12) == IMAGE_REL_BASED_HIGHLOW)
					{
						//Get DWORD at relocation address
						DWORD* value = reinterpret_cast<DWORD*>(original_image_base + reloc->VirtualAddress + (elem & ((1 << 12) - 1)));
						//Fix it like PE loader
						*value = *value + locationDelta;
					}
				}
				//Go to next relocation table
				reloc = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(reinterpret_cast<const char*>(reloc) + reloc->SizeOfBlock);
			}
		}

	}
	//*/
	//FIXME:
	//Pointer to TLS directory
	IMAGE_DATA_DIRECTORY* tls_dir;
	tls_dir = reinterpret_cast<IMAGE_DATA_DIRECTORY*>(offset_to_directories + sizeof(IMAGE_DATA_DIRECTORY) * IMAGE_DIRECTORY_ENTRY_TLS);

	if(tls_dir->VirtualAddress)
	{
		//*
		PIMAGE_TLS_DIRECTORY tls;
		tls = reinterpret_cast<PIMAGE_TLS_DIRECTORY>(original_image_base + tls_dir->VirtualAddress);
		//If TLS has callbacks
		PIMAGE_TLS_CALLBACK* tls_callback_address;
		//Pointer to first callback of an original array
		tls_callback_address = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(tls->AddressOfCallBacks);

		while(true)
		{
			//If callback is null - this is the end of array
			if(!*tls_callback_address)
				break;

			//Execute callback
			(*tls_callback_address)(reinterpret_cast<PVOID>(original_image_base), DLL_PROCESS_ATTACH, 0);

			//Move to next callback
			++tls_callback_address;
		}
		//*/
	}
	
	
	//Restore headers memory attributes
	virtual_protect(reinterpret_cast<LPVOID>(original_image_base), rva_of_first_section, old_protect, &old_protect);

	//Create epilogue manually
	_asm
	{
		//Move to original entry point
		mov eax, info_copy.original_entry_point;
		add eax, original_image_base;
		leave;
		//Like this
		jmp eax;
	}
}