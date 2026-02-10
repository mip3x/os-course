# ELF

## Содержание

- [Ресурсы](#Ресурсы)
- [Основные сущности](#Основные-сущности)
- [Различие сегментов и секций](#Различие-сегментов-и-секций)
- [Заголовок `ELF`](#Заголовок-ELF)
- [Заголовки секций](#Заголовки-секций)
- [Заголовки сегментов (заголовки программ)](#Заголовки-сегментов-заголовки-программ)
    - [Структура `Program Header`'а](#Структура-Program-Headerа)

## Ресурсы

1) [Подробное видео на тему](https://youtu.be/nC1U1LJQL8o?si=a_rvF1k9vZImVYbV)
2) [Хорошая статья по структурам `ELF`](https://tmpout.sh/4/12.html)
3) [Исходный код описания структур из ядра](https://elixir.bootlin.com/linux/v6.18.6/source/include/uapi/linux/elf.h)
4) [man-страница](https://man7.org/linux/man-pages/man5/elf.5.html)
5) [Спецификация `ELF`](https://refspecs.linuxfoundation.org/elf/elf.pdf)

## Основные сущности

Двумя основными сущностями, содержащимися в `ELF`-файле являются:
1) Сегменты (segments)
2) Секции (sections)

Каждый `ELF` может содержать 0 или более сегментов и 0 или более секций. Несмотря на то, что эти два слова являются синонимичными, в контексте `ELF` это 2 совершенно разных понятия.

Сегменты используются в `run-time`, секции - при линковке (`link-time`).

![Linking View & Execution View](./linking_execution_view.png)

Как сегменты, так и секции указывают на абсолютный адрес внутри `ELF`-файла и описывают, сколько байт содержат внутри себя. Также, существуют сегменты, не содержащие никакого контента вовсе.

![Сегменты и секции внутри `ELF`](./segments_sections_inside_elf_diagram.png)

## Различие сегментов и секций

Главное отличие сегмента от секции заключается в том, что сегменты описывают, где и как они должны быть загружены в виртуальную или физическую память: то есть, сегменты сообщают ОС, как она должна замапить части `ELF` в память.

## Заголовок `ELF`

```c
typedef struct {
    unsigned char e_ident[16]; /*  ELF identification */
    Elf64_Half e_type;         /*  Object file type */
    Elf64_Half e_machine;      /*  Machine type */
    Elf64_Word e_version;      /*  Object file version */
    Elf64_Addr e_entry;        /*  Entry point address */
    Elf64_Off  e_phoff;        /*  Program header offset */
    Elf64_Off  e_shoff;        /*  Section header offset */
    Elf64_Word e_flags;        /*  Processor-specific flags */
    Elf64_Half e_ehsize;       /*  ELF header size */
    Elf64_Half e_phentsize;    /*  Size of program header entry */
    Elf64_Half e_phnum;        /*  Number of program header entries */
    Elf64_Half e_shentsize;    /*  Size of section header entry */
    Elf64_Half e_shnum;        /*  Number of section header entries */
    Elf64_Half e_shstrndx;     /*  Section name string table index */
} Elf64_Ehdr
```

`e_entry` содержит виртуальный адрес точки входа в программу (метки `_start`). Для `no-pie` бинарников адрес чётко определён. Для `pie` здесь будет содержаться сдвиг относительно начала секции `.text`. `PIE` - позиционно-независимый, `no PIE` - позиционно-зависимый

`e_type` определяет тип `ELF`-файла:
0) `ET_NONE`: неизвестный тип
1) `ET_REL`: перемещаемый (`relocatable`) файл
2) `ET_EXEC`: исполняемый файл (не `PIE`)
3) `ET_DYN`: исполняемый файл или разделяемый объект (`PIE`)
4) `ET_CORE`: файл ядра

## Заголовки секций

Разделы кода и данных, из которых состоит программа. Внутри `ELF`'а описывающие структуры секций находятся в массиве друг за другом, их смещение в файле определяет поле `e_shoff`, размер - `e_shentsize`, количество - `e_shnum`.

Секции используются во время линковки, во время исполнения - нет.

Структура, описывающая заголовок секции:
```c
typedef struct {
    uint32_t   sh_name;
    uint32_t   sh_type;
    uint64_t   sh_flags;
    Elf64_Addr sh_addr;
    Elf64_Off  sh_offset;
    uint64_t   sh_size;
    uint32_t   sh_link;
    uint32_t   sh_info;
    uint64_t   sh_addralign;
    uint64_t   sh_entsize;
} Elf64_Shdr;
```

`sh_name` - не строка, а сдвиг внутри таблицы строк секций (`section header string table`). Эта таблица адресуема по сдвигу `e_shstrndx` (поле заголовка `ELF`): он описывает индекс секции, в которой хранится таблица. Она содержит NULL-терминированные строки. Пример:
```
E  x  a  m  p  l  e     T  a  b  l  e
45 78 61 6d 70 6c 65 00 54 61 62 6c 65 00
```

`Example` имеет сдвиг 0, `Table` - сдвиг 8.

`sh_type` описывает тип секции. Ниже несколько (не все):
```
SHT_NULL            - 0
    This value marks the section header as inactive. It
    does not have an associated section. Other members
    of the section header have undefined values.

SHT_PROGBITS        - 1
    This section holds information defined by the
    program, whose format and meaning are determined
    solely by the program.

SHT_SYMTAB          - 2
    This section holds a symbol table. Typically,
    SHT_SYMTAB provides symbols for link editing, though
    it may also be used for dynamic linking. As a
    complete symbol table, it may contain many symbols
    unnecessary for dynamic linking. An object file can
    also contain a SHT_DYNSYM section. The index of the
    associated string table section can be found in the
    sh_link member.

SHT_STRTAB          - 3
    This section holds a string table. An object file
    may have multiple string table sections.

SHT_RELA            - 4
    This section holds relocation entries with explicit
    addends, such as type Elf32_Rela for the 32-bit
    class of object files. An object may have multiple
    relocation sections.

SHT_HASH            - 5
    This section holds a symbol hash table. An object
    participating in dynamic linking must contain a
    symbol hash table. An object file may have only one
    hash table.

SHT_DYNAMIC         - 6
    This section holds information for dynamic linking.
    An object file may have only one dynamic section.

SHT_NOTE            - 7
    This section holds notes (ElfN_Nhdr).

SHT_NOBITS          - 8
    A section of this type occupies no space in the file
    but otherwise resembles SHT_PROGBITS. Although this
    section contains no bytes, the sh_offset member
    contains the conceptual file offset.

SHT_REL             - 9
    This section holds relocation offsets without
    explicit addends, such as type Elf32_Rel for the
    32-bit class of object files. An object file may
    have multiple relocation sections.

SHT_SHLIB           - 10
    This section is reserved but has unspecified
    semantics.

SHT_DYNSYM          - 11
    This section holds a minimal set of dynamic linking
    symbols. An object file can also contain a
    SHT_SYMTAB section.
```

`sh_flags` описывает флаги:
- `SHF_WRITE`: эта секция может быть записываемой, если из неё создаётся сегмент
- `SHF_ALLOC`: эта секция должна быть частью сегментов, которые загружаются в память
- `SHF_EXECINSTR`: эта секция содержит исполняемый код

`sh_addr` содержит адрес, на котором секция будет находиться в памяти процесса. Если секция не должна загружаться в память, то поле будет равно 0

`sh_offset` содержит сдвиг в `ELF`-файле, определяющий начало данных секции

`sh_size` содержит размер данных секции

`sh_link` позволяет секции быть слинкованной с другой секцией (?)

> This member holds a section header table index link, whose interpretation depends on the section type

`sh_info` содержит дополнительную информацию, интерпретация зависит от типа секции

`sh_addralign` определяет ограничения на выравнивание секции

`sh_entsize` используется для некоторых секций, содержащих таблицы с записями фиксированного размера. Хранит размер одной такой записи

> Some sections hold a table of fixed-sized entries, such as a symbol table. For such a section, this member gives the size in bytes for each entry. This member contains zero if the section does not hold a table of fixed-size entries.

Получить описание секций можно с помощью вызова программы `readelf` с флагами `--sections/-S`.

После этапа компоновки все эти поля не имеют более никакой ценности, поэтому присутствие раздела заголовков секций внутри исполняемого файла не является обязательным

## Заголовки сегментов (заголовки программ)

Содержат информацию о сегментах, которые необходимо загрузить в память. Находятся сразу же после `ELF`-заголовка. Каждый сегмент определён заголовком `Program Header`. Все эти `PH`'ы идут подряд внутри `ELF`, поле `e_phoff` содержит сдвиг начала этого массива `PH` внутри `ELF`. Полу `e_phentsize` определяет размер одного такого `PH`, `e_phnum` - их количество.

![`Program Headers`](./program_headers.png)

### Структура `Program Header`'а

```c
typedef struct {
    uint32_t   p_type;
    uint32_t   p_flags;
    Elf64_Off  p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    uint64_t   p_filesz;
    uint64_t   p_memsz;
    uint64_t   p_align;
} Elf64_Phdr;
```

Важным полем является `p_type`, описывающий тип сегмента. Этих типов всего 8:
```
PT_NULL         - 0 - PLACE HOLDER
PT_LOAD         - 1 - LOADABLE SEGMENT
PT_DYNAMIC      - 2 - INFO FOR DYNAMIC LINKING
PT_INTERP       - 3 - LOCATION OF INTERPRETER
PT_NOTE         - 4 - METADATA
PT_PHDR         - 6 - LOCATION OF PROGRAM HEADER TABLE IN MEMORY
PT_TLS          - 7 - THREAD LOCAL STORAGE INFORMATION
```

Касательно `PT_INTERP`: сегменты этого типа будут иметь динамические бинарники, но НЕ будут иметь разделяемые библиотеки. 

`p_offset` содержит смещение сегмента относительно начала самого `ELF`

`p_vaddr` содержит виртуальный адрес, по которому должен находиться первый байт сегмента в памяти
  
`p_paddr` содержит тот же самый адрес, но физический (к примеру, используется в прошивках)

`p_filesz` и `p_memsz` определяют размер сегмента в бинарнике `ELF` и в памяти соответственно

`p_flags` определяет права для сегмента (комбинация `R/W/E`)

`p_align` определяет выравнивание

Получить описание сегментов можно с помощью вызова программы `readelf` с флагами `--segments/-l`.
